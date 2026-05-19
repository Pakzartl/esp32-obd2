#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/twai.h"
#include "esp_timer.h"

// ADV350 vehicle constants
#define ADV350_DISPLACEMENT_CC    329.6f
#define ADV350_CURB_WEIGHT_KG     238.0f
#define ADV350_REAR_CIRC_M        1.6091f
#define ADV350_FRONT_CIRC_M       1.6701f
#define ADV350_FUEL_TANK_L        11.0f
#define ADV350_FUEL_DENSITY_KG_L  0.740f
#define ADV350_DRAG_CD            0.65f
#define ADV350_FRONTAL_AREA_M2    0.55f
#define ADV350_ROLL_RESIST_CR     0.015f
#define ADV350_MAX_POWER_KW       26.0f
#define ADV350_MAX_TORQUE_NM      32.1f
#define ADV350_FINAL_DRIVE_RATIO  8.892f
#define ADV350_DEFAULT_VE         0.82f
#define ADV350_STOICH_AFR         14.7f

// OBD2 CAN IDs
#define OBD2_REQUEST_ID    0x7DF
#define OBD2_RESPONSE_ID   0x7E8

// OBD2 Mode 01 PIDs
#define PID_SUPPORTED_01_20   0x00
#define PID_MONITOR_STATUS    0x01
#define PID_FUEL_SYS_STATUS   0x03
#define PID_ENGINE_LOAD       0x04
#define PID_COOLANT_TEMP      0x05
#define PID_MAP_KPA           0x0B
#define PID_RPM               0x0C
#define PID_SPEED             0x0D
#define PID_INTAKE_AIR_TEMP   0x0F
#define PID_THROTTLE          0x11
#define PID_SUPPORTED_21_40   0x20
#define PID_FUEL_LEVEL        0x2F
#define PID_BATTERY_VOLTAGE   0x42
#define PID_FUEL_RATE         0x5E
#define PID_SUPPORTED_41_60   0x40
#define PID_SUPPORTED_61_80   0x60

// Stale threshold (microseconds) — value invalid after this
#define SENSOR_STALE_US  3000000  // 3 seconds

// Sensor value with validity tracking
typedef struct {
    float value;
    bool valid;
    int64_t updated_at;
} sensor_t;

static inline bool sensor_fresh(const sensor_t *s) {
    return s->valid && (esp_timer_get_time() - s->updated_at) < SENSOR_STALE_US;
}

static inline void sensor_set(sensor_t *s, float val) {
    s->value = val;
    s->valid = true;
    s->updated_at = esp_timer_get_time();
}

static inline void sensor_invalidate(sensor_t *s) {
    s->valid = false;
}

// Vehicle data — updated by decoder from CAN frames
typedef struct {
    // Standard OBD2 PIDs
    sensor_t rpm;
    sensor_t speed_kmh;
    sensor_t coolant_temp_c;
    sensor_t throttle_pct;
    sensor_t engine_load_pct;
    sensor_t map_kpa;
    sensor_t intake_air_temp_c;
    sensor_t battery_v;
    sensor_t fuel_rate_lph;      // PID 0x5E if available
    sensor_t fuel_level_pct;

    // Honda proprietary broadcast (populated after RE)
    sensor_t honda_rpm;
    sensor_t honda_speed;
    sensor_t honda_throttle;
    sensor_t honda_coolant;
    sensor_t honda_lambda;
    sensor_t honda_injector_pw_us;
    sensor_t wheel_speed_front;
    sensor_t wheel_speed_rear;

    // Supported PIDs bitmap (4 groups of 32 bits)
    uint32_t supported_pids[4];
    bool pids_probed;

    // DTCs
    struct {
        uint16_t raw;
        char text[6];   // "P0130\0"
    } dtcs[16];
    uint8_t dtc_count;
    bool dtcs_valid;
} vehicle_data_t;

// Best-effort getters: prefer Honda broadcast (faster), fallback to OBD2
static inline float vd_rpm(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->honda_rpm)) return vd->honda_rpm.value;
    if (sensor_fresh(&vd->rpm)) return vd->rpm.value;
    return -1;
}

static inline float vd_speed(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->honda_speed)) return vd->honda_speed.value;
    if (sensor_fresh(&vd->speed_kmh)) return vd->speed_kmh.value;
    return -1;
}

static inline float vd_throttle(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->honda_throttle)) return vd->honda_throttle.value;
    if (sensor_fresh(&vd->throttle_pct)) return vd->throttle_pct.value;
    return -1;
}

static inline float vd_coolant(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->honda_coolant)) return vd->honda_coolant.value;
    if (sensor_fresh(&vd->coolant_temp_c)) return vd->coolant_temp_c.value;
    return -1;
}

static inline float vd_map(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->map_kpa)) return vd->map_kpa.value;
    return -1;
}

static inline float vd_iat(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->intake_air_temp_c)) return vd->intake_air_temp_c.value;
    return 25.0f; // default ambient
}

// Global vehicle data (defined in obd2.c)
extern vehicle_data_t g_vehicle;

// OBD2 engine
void obd2_init(void);
void obd2_process_frame(const twai_message_t *msg);
bool obd2_is_pid_supported(uint8_t pid);
void obd2_probe_supported_pids(void);
void obd2_request_pid(uint8_t pid);
void obd2_read_dtcs(void);
void obd2_poll_task(void *arg);

// Honda broadcast decoder
void honda_decode_frame(uint32_t can_id, const uint8_t *data, uint8_t dlc);
