#include "metrics.h"
#include "obd2.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "METRICS";

metrics_t g_metrics = {0};

static float s_rider_weight_kg = 75.0f;
static float s_ve = ADV350_DEFAULT_VE;
static int64_t s_last_update_us = 0;

// Savitzky-Golay 5-point derivative filter for smooth acceleration
#define SPEED_HIST_SIZE 5
static float s_speed_hist[SPEED_HIST_SIZE] = {0};
static int64_t s_speed_time[SPEED_HIST_SIZE] = {0};
static uint8_t s_speed_idx = 0;
static uint8_t s_speed_count = 0;

// Riding score accumulators
static float s_throttle_jerk_acc = 0;
static float s_brake_jerk_acc = 0;
static float s_last_throttle = 0;
static uint32_t s_hard_events = 0;
static uint32_t s_score_samples = 0;
static float s_last_accel = 0;

// Braking state
static float s_brake_start_speed = 0;
static float s_brake_dist_acc = 0;

static float total_mass(void)
{
    return ADV350_CURB_WEIGHT_KG + s_rider_weight_kg;
}

// Speed-Density fuel rate: MAP + RPM + IAT → L/h
static float calc_fuel_rate_speed_density(float map_kpa, float rpm, float iat_c, float lambda)
{
    if (rpm < 50 || map_kpa < 5) return 0;
    float t_kelvin = iat_c + 273.15f;
    float m_air_per_stroke = (map_kpa * 1000.0f * ADV350_DISPLACEMENT_CC * 1e-6f * s_ve)
                             / (287.05f * t_kelvin);
    float strokes_per_sec = rpm / 120.0f;  // 4-stroke single cylinder
    float m_fuel_kg_s = (m_air_per_stroke * strokes_per_sec) / (ADV350_STOICH_AFR * lambda);
    return (m_fuel_kg_s / ADV350_FUEL_DENSITY_KG_L) * 3600.0f;
}

// Engine load + RPM fallback method
static float calc_fuel_rate_load_rpm(float load_pct, float rpm)
{
    if (rpm < 50) return 0;
    // K = rho_air * Vd * 3600 / (100 * 120 * AFR * rho_fuel)
    const float K = 1.17675e-5f;
    return K * load_pct * rpm;
}

static void push_speed(float speed_kmh)
{
    s_speed_hist[s_speed_idx] = speed_kmh / 3.6f; // m/s
    s_speed_time[s_speed_idx] = esp_timer_get_time();
    s_speed_idx = (s_speed_idx + 1) % SPEED_HIST_SIZE;
    if (s_speed_count < SPEED_HIST_SIZE) s_speed_count++;
}

// 5-point Savitzky-Golay derivative: a = (2*v[t+2]+v[t+1]-v[t-1]-2*v[t-2]) / (10*dt)
static float calc_acceleration(void)
{
    if (s_speed_count < SPEED_HIST_SIZE) return 0;

    // Indices: newest = idx-1, oldest = idx-5
    int i0 = (s_speed_idx + 0) % SPEED_HIST_SIZE; // oldest (t-2)
    int i1 = (s_speed_idx + 1) % SPEED_HIST_SIZE; // t-1
    int i3 = (s_speed_idx + 3) % SPEED_HIST_SIZE; // t+1
    int i4 = (s_speed_idx + 4) % SPEED_HIST_SIZE; // t+2 (newest)

    float dt_us = (float)(s_speed_time[i4] - s_speed_time[i0]);
    if (dt_us < 1000) return 0; // guard div-by-zero

    float dt_s = dt_us / 1e6f;
    float dt_avg = dt_s / 4.0f; // average sample interval

    float accel = (2.0f * s_speed_hist[i4] + s_speed_hist[i3]
                   - s_speed_hist[i1] - 2.0f * s_speed_hist[i0])
                  / (10.0f * dt_avg);
    return accel;
}

void metrics_update(void)
{
    int64_t now = esp_timer_get_time();
    float dt_s = (s_last_update_us > 0) ? (float)(now - s_last_update_us) / 1e6f : 0;
    s_last_update_us = now;
    if (dt_s <= 0 || dt_s > 2.0f) dt_s = 0; // skip first call or stale gap

    float rpm = vd_rpm(&g_vehicle);
    float speed = vd_speed(&g_vehicle);
    float throttle = vd_throttle(&g_vehicle);

    g_metrics.valid = (rpm >= 0 && speed >= 0);
    if (!g_metrics.valid) return;

    // Acceleration (Savitzky-Golay filtered)
    push_speed(speed);
    g_metrics.accel_ms2 = calc_acceleration();
    g_metrics.gforce_x = g_metrics.accel_ms2 / 9.81f;

    // Fuel consumption — pick best available method
    float fuel_rate = 0;
    if (sensor_fresh(&g_vehicle.fuel_rate_lph)) {
        // Best: direct from ECU (PID 0x5E)
        fuel_rate = g_vehicle.fuel_rate_lph.value;
    } else {
        float map = vd_map(&g_vehicle);
        float iat = vd_iat(&g_vehicle);
        float lambda = sensor_fresh(&g_vehicle.lambda) ? g_vehicle.lambda.value : 1.0f;

        if (map > 0) {
            fuel_rate = calc_fuel_rate_speed_density(map, rpm, iat, lambda);
        } else if (sensor_fresh(&g_vehicle.engine_load_pct)) {
            fuel_rate = calc_fuel_rate_load_rpm(g_vehicle.engine_load_pct.value, rpm);
        }
    }

    // Fuel cut detection: throttle closed + engine braking
    if (throttle >= 0 && throttle < 2.0f && rpm > 2000) {
        fuel_rate = 0;
    }

    g_metrics.fuel_rate_lph = fuel_rate;
    g_metrics.fuel_consumption_kmpl = (fuel_rate > 0.01f && speed > 1.0f)
        ? speed / fuel_rate : 0;

    // Trip accumulation
    if (dt_s > 0) {
        g_metrics.trip_fuel_l += fuel_rate * dt_s / 3600.0f;
        g_metrics.trip_dist_km += speed * dt_s / 3600.0f;
        g_metrics.trip_avg_kmpl = (g_metrics.trip_fuel_l > 0.001f)
            ? g_metrics.trip_dist_km / g_metrics.trip_fuel_l : 0;
    }

    // CVT ratio (engine RPM / wheel RPM * final drive)
    if (speed > 5.0f && rpm > 500) {
        float v_ms = speed / 3.6f;
        float wheel_rps = v_ms / ADV350_REAR_CIRC_M;
        float wheel_rpm = wheel_rps * 60.0f;
        if (wheel_rpm > 1) {
            g_metrics.cvt_ratio = rpm / wheel_rpm;
        }
    } else {
        g_metrics.cvt_ratio = 0;
    }

    // Wheel power: P = (M*a + aero_drag + rolling_resistance) * v
    float v_ms = speed / 3.6f;
    if (v_ms > 0.5f) {
        float M = total_mass();
        float f_accel = M * g_metrics.accel_ms2;
        float f_aero = 0.5f * 1.225f * ADV350_DRAG_CD * ADV350_FRONTAL_AREA_M2 * v_ms * v_ms;
        float f_roll = ADV350_ROLL_RESIST_CR * M * 9.81f;
        g_metrics.wheel_power_kw = (f_accel + f_aero + f_roll) * v_ms / 1000.0f;
        if (g_metrics.wheel_power_kw < 0) g_metrics.wheel_power_kw = 0;
    } else {
        g_metrics.wheel_power_kw = 0;
    }

    // Braking detection + distance
    bool was_braking = g_metrics.is_braking;
    g_metrics.is_braking = (g_metrics.accel_ms2 < -1.5f && speed > 2.0f);

    if (g_metrics.is_braking && !was_braking) {
        s_brake_start_speed = speed;
        s_brake_dist_acc = 0;
    }
    if (g_metrics.is_braking && dt_s > 0) {
        s_brake_dist_acc += v_ms * dt_s;
        g_metrics.braking_dist_m = s_brake_dist_acc;
    }
    if (!g_metrics.is_braking && was_braking && s_brake_dist_acc > 0.5f) {
        ESP_LOGI(TAG, "Brake: %.0f km/h -> stop in %.1f m",
                 s_brake_start_speed, s_brake_dist_acc);
    }

    // Riding score (exponential moving average)
    if (dt_s > 0 && throttle >= 0) {
        s_score_samples++;
        float throttle_jerk = fabsf(throttle - s_last_throttle) / dt_s;
        s_throttle_jerk_acc += throttle_jerk;
        s_last_throttle = throttle;

        float accel_jerk = fabsf(g_metrics.accel_ms2 - s_last_accel) / dt_s;
        s_brake_jerk_acc += accel_jerk;
        s_last_accel = g_metrics.accel_ms2;

        if (fabsf(g_metrics.accel_ms2) > 4.0f) s_hard_events++;

        if (s_score_samples >= 50) {
            float avg_throttle_jerk = s_throttle_jerk_acc / s_score_samples;
            float avg_brake_jerk = s_brake_jerk_acc / s_score_samples;
            float hard_rate = (float)s_hard_events / s_score_samples * 100.0f;

            // Score: 100 = perfectly smooth
            float score = 100.0f;
            score -= fminf(avg_throttle_jerk * 0.3f, 25.0f);
            score -= fminf(avg_brake_jerk * 0.2f, 25.0f);
            score -= fminf(hard_rate * 5.0f, 30.0f);
            if (score < 0) score = 0;

            g_metrics.riding_score = (uint8_t)score;

            s_throttle_jerk_acc = 0;
            s_brake_jerk_acc = 0;
            s_hard_events = 0;
            s_score_samples = 0;
        }
    }
}

void metrics_init(void)
{
    memset(&g_metrics, 0, sizeof(g_metrics));
    s_last_update_us = 0;
    s_speed_count = 0;
    s_speed_idx = 0;
    s_score_samples = 0;
    s_throttle_jerk_acc = 0;
    s_brake_jerk_acc = 0;
    s_hard_events = 0;
    ESP_LOGI(TAG, "Metrics initialized (rider=%.0fkg, VE=%.2f)", s_rider_weight_kg, s_ve);
}

void metrics_reset_trip(void)
{
    g_metrics.trip_fuel_l = 0;
    g_metrics.trip_dist_km = 0;
    g_metrics.trip_avg_kmpl = 0;
    ESP_LOGI(TAG, "Trip reset");
}

void metrics_set_rider_weight(float kg)
{
    if (kg > 20 && kg < 200) {
        s_rider_weight_kg = kg;
        ESP_LOGI(TAG, "Rider weight: %.0f kg", kg);
    }
}

void metrics_set_ve(float ve)
{
    if (ve > 0.5f && ve < 1.0f) {
        s_ve = ve;
        ESP_LOGI(TAG, "VE calibrated: %.3f", ve);
    }
}
