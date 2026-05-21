#include "smart_features.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/temperature_sensor.h"

static const char *TAG = "SMART";

trip_info_t g_trip = {0};
alert_state_t g_alerts = {0};

static temperature_sensor_handle_t s_temp_handle = NULL;
static int64_t s_rpm_nonzero_since = 0;
static int64_t s_rpm_zero_since = 0;

// Trip: RPM>0 for 5s = start, RPM=0 for 60s = end
#define TRIP_START_DELAY_US   5000000
#define TRIP_END_DELAY_US    60000000

void smart_features_init(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_temp_handle) == ESP_OK) {
        temperature_sensor_enable(s_temp_handle);
        ESP_LOGI(TAG, "Board temp sensor ready");
    }

    memset(&g_trip, 0, sizeof(g_trip));
    memset(&g_alerts, 0, sizeof(g_alerts));
    g_trip.state = TRIP_IDLE;
}

float smart_get_board_temp(void)
{
    float temp = 0;
    if (s_temp_handle) {
        temperature_sensor_get_celsius(s_temp_handle, &temp);
    }
    return temp;
}

static void update_alerts(const relay_packet_t *pkt)
{
    int coolant = (pkt->vd_flags & RF_COOLANT) ? pkt->vd_coolant_enc - 40 : 0;
    int rpm = (pkt->vd_flags & RF_RPM) ? pkt->vd_rpm : 0;
    int speed = (pkt->vd_flags & RF_SPEED) ? pkt->vd_speed : 0;

    bool prev_any = g_alerts.any_active;

    g_alerts.coolant_warn = coolant >= ALERT_COOLANT_WARN && coolant < ALERT_COOLANT_CRIT;
    g_alerts.coolant_crit = coolant >= ALERT_COOLANT_CRIT;
    g_alerts.rpm_high = rpm >= ALERT_RPM_HIGH;
    g_alerts.speed_limit = speed >= ALERT_SPEED_LIMIT;
    g_alerts.any_active = g_alerts.coolant_crit || g_alerts.rpm_high || g_alerts.speed_limit;

    if (g_alerts.any_active && !prev_any) {
        ESP_LOGW(TAG, "ALERT: CLT=%d RPM=%d SPD=%d", coolant, rpm, speed);
    }
    if (g_alerts.coolant_crit) {
        ESP_LOGE(TAG, "OVERHEAT! Coolant=%d°C", coolant);
    }
}

static void update_trip(const relay_packet_t *pkt)
{
    int rpm = (pkt->vd_flags & RF_RPM) ? pkt->vd_rpm : 0;
    int64_t now = esp_timer_get_time();

    if (rpm > 0) {
        s_rpm_zero_since = 0;
        if (s_rpm_nonzero_since == 0) s_rpm_nonzero_since = now;
    } else {
        s_rpm_nonzero_since = 0;
        if (s_rpm_zero_since == 0) s_rpm_zero_since = now;
    }

    switch (g_trip.state) {
    case TRIP_IDLE:
        if (s_rpm_nonzero_since > 0 &&
            (now - s_rpm_nonzero_since) > TRIP_START_DELAY_US) {
            g_trip.state = TRIP_ACTIVE;
            g_trip.start_us = now;
            g_trip.trip_count++;
            g_trip.active_seconds = 0;
            ESP_LOGI(TAG, "Trip #%lu started", (unsigned long)g_trip.trip_count);
        }
        break;

    case TRIP_ACTIVE:
        if (rpm > 0) {
            g_trip.active_seconds = (uint32_t)((now - g_trip.start_us) / 1000000);
        }
        if (s_rpm_zero_since > 0 &&
            (now - s_rpm_zero_since) > TRIP_END_DELAY_US) {
            g_trip.state = TRIP_IDLE;
            g_trip.end_us = now;
            uint32_t dur = (uint32_t)((g_trip.end_us - g_trip.start_us) / 1000000);
            ESP_LOGI(TAG, "Trip #%lu ended (%lus)",
                     (unsigned long)g_trip.trip_count, (unsigned long)dur);
        }
        break;

    default:
        g_trip.state = TRIP_IDLE;
        break;
    }
}

void smart_features_update(const relay_packet_t *pkt)
{
    update_alerts(pkt);
    update_trip(pkt);
}
