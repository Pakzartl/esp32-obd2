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

// Honda UDS CAN IDs (29-bit extended) — confirmed working on ADV350
#define HONDA_UDS_REQUEST_ID   0x18DA10F1   // Tester(F1) → ECU(10)
#define HONDA_UDS_RESPONSE_ID  0x18DAF110   // ECU(10) → Tester(F1)
#define HONDA_UDS_FUNC_ID      0x18DB33F1   // Functional broadcast

// UDS Service IDs
#define UDS_DIAG_SESSION       0x10
#define UDS_ECU_RESET          0x11
#define UDS_READ_DATA_BY_ID    0x22
#define UDS_READ_DTC           0x19
#define UDS_TESTER_PRESENT     0x3E
#define UDS_POSITIVE_OFFSET    0x40   // response = service + 0x40

// UDS Diagnostic Session types
#define UDS_SESSION_DEFAULT    0x01
#define UDS_SESSION_EXTENDED   0x03

// Honda UDS DIDs (Data Identifiers) — to be discovered/confirmed
// Common Keihin DIDs from community research
#define DID_RPM                0xF40C
#define DID_SPEED              0xF40D
#define DID_COOLANT_TEMP       0xF405
#define DID_THROTTLE           0xF411
#define DID_ENGINE_LOAD        0xF404
#define DID_MAP_KPA            0xF40B
#define DID_INTAKE_AIR_TEMP    0xF40F
// Unsupported by ADV350 ECU (NRC 0x31 confirmed):
// 0xF442 Battery Voltage, 0xF45E Fuel Rate,
// 0xF401, 0xF402, 0xF403, 0xF406, 0xF407, 0xF40E, 0xF41C

// Multi-frame responses (ISO-TP required)
#define DID_MONITOR_STATUS     0x0100  // 59 bytes, confirmed multi-frame
#define DID_LAMBDA             0x0124  // confirmed multi-frame

// ISO-TP multi-frame reassembly
#define ISOTP_MAX_LEN  64
typedef struct {
    uint8_t buf[ISOTP_MAX_LEN];
    uint16_t expected_len;
    uint16_t received_len;
    uint8_t next_seq;
    bool active;
} isotp_rx_t;

// Stale threshold (microseconds)
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

// Vehicle data — updated by UDS responses
typedef struct {
    sensor_t rpm;
    sensor_t speed_kmh;
    sensor_t coolant_temp_c;
    sensor_t throttle_pct;
    sensor_t engine_load_pct;
    sensor_t map_kpa;
    sensor_t intake_air_temp_c;
    sensor_t battery_v;
    sensor_t fuel_rate_lph;
    sensor_t fuel_level_pct;
    sensor_t lambda;
    sensor_t injector_pw_us;

    bool session_active;
    bool dids_probed;
    uint16_t supported_dids[32];
    uint8_t supported_did_count;

    // DTCs
    struct {
        uint16_t raw;
        char text[6];
    } dtcs[16];
    uint8_t dtc_count;
    bool dtcs_valid;
} vehicle_data_t;

// Best-effort getters
static inline float vd_rpm(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->rpm)) return vd->rpm.value;
    return -1;
}

static inline float vd_speed(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->speed_kmh)) return vd->speed_kmh.value;
    return -1;
}

static inline float vd_throttle(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->throttle_pct)) return vd->throttle_pct.value;
    return -1;
}

static inline float vd_coolant(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->coolant_temp_c)) return vd->coolant_temp_c.value;
    return -1;
}

static inline float vd_map(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->map_kpa)) return vd->map_kpa.value;
    return -1;
}

static inline float vd_iat(const vehicle_data_t *vd) {
    if (sensor_fresh(&vd->intake_air_temp_c)) return vd->intake_air_temp_c.value;
    return 25.0f;
}

// Global vehicle data
extern vehicle_data_t g_vehicle;

// Scan pause flag (defined in main.c, used by poll task)
extern volatile bool scan_pause_others;

// UDS engine
void obd2_init(void);
void obd2_process_frame(const twai_message_t *msg);
void obd2_start_session(void);
void obd2_read_did(uint16_t did);
void obd2_tester_present(void);
void obd2_read_dtcs(void);
void obd2_probe_dids(void);
void obd2_poll_task(void *arg);
