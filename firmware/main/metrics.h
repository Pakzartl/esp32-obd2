#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float accel_ms2;              // longitudinal acceleration
    float gforce_x;               // longitudinal g-force
    float fuel_rate_lph;          // instantaneous fuel rate (L/h)
    float fuel_consumption_kmpl;  // instantaneous km/L (0 if stopped)
    float trip_fuel_l;            // accumulated fuel used this trip
    float trip_dist_km;           // accumulated distance this trip
    float trip_avg_kmpl;          // trip average km/L
    float wheel_power_kw;         // estimated wheel power
    float cvt_ratio;              // effective CVT ratio
    float braking_dist_m;         // current braking event distance
    uint8_t riding_score;         // 0-100 riding smoothness
    bool is_braking;
    bool valid;                   // at least RPM+speed available
} metrics_t;

extern metrics_t g_metrics;

void metrics_init(void);
void metrics_update(void);               // call at 10-20 Hz
void metrics_reset_trip(void);
void metrics_set_rider_weight(float kg);  // default 75 kg
void metrics_set_ve(float ve);            // volumetric efficiency calibration
