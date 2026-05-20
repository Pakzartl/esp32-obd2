#pragma once
#include <stdint.h>

// ESP-NOW wire format: ESP32 CAN board --> ESP32-S3 BLE relay
// Payload ~44 bytes, well within ESP-NOW 250-byte limit.
// Values are pre-encoded to match BLE GATT packet format so the
// relay can forward with zero re-encoding.

#define RELAY_MAGIC    0xAD
#define RELAY_VERSION  1

// vd_flags bits (same as BLE vehicle_data flags)
#define RF_RPM       (1 << 0)
#define RF_SPEED     (1 << 1)
#define RF_COOLANT   (1 << 2)
#define RF_THROTTLE  (1 << 3)
#define RF_MAP       (1 << 4)
#define RF_IAT       (1 << 5)
#define RF_BATTERY   (1 << 6)
#define RF_METRICS   (1 << 7)

typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  seq;
    uint8_t  _pad;
    // -- vehicle (BLE-ready encoded) --
    uint16_t vd_flags;
    uint16_t vd_rpm;
    uint8_t  vd_speed;
    uint8_t  vd_coolant_enc;      // celsius + 40
    uint8_t  vd_throttle_enc;     // percent * 2.55
    uint8_t  vd_map;              // kPa
    uint8_t  vd_iat_enc;          // celsius + 40
    uint8_t  _pad2;
    uint16_t vd_battery_cv;       // volt * 100
    uint16_t vd_fuel_rate_x100;   // L/h * 100
    uint16_t vd_cvt_x100;        // ratio * 100
    uint8_t  vd_score;            // 0-100
    uint8_t  metrics_valid;
    // -- metrics (int16 matching BLE metrics packet) --
    int16_t  mt_accel_x100;
    int16_t  mt_gforce_x1000;
    int16_t  mt_fuel_kmpl_x10;
    int16_t  mt_trip_fuel_x1000;
    int16_t  mt_trip_dist_x100;
    int16_t  mt_trip_avg_x10;
    int16_t  mt_power_x100;
    int16_t  mt_brake_dist_x10;
} relay_packet_t;

// Control command: S3 --> ESP32
#define RELAY_CMD_MAGIC  0xAE

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t cmd;        // same byte values as BLE control (0x10-0x13)
    uint8_t data[4];
} relay_cmd_t;
