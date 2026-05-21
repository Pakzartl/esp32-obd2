#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "relay_packet.h"

typedef struct __attribute__((packed)) {
    uint32_t timestamp_s;
    uint16_t trip_id;
    uint16_t vd_flags;
    uint16_t rpm;
    uint8_t  speed;
    uint8_t  coolant_enc;
    uint8_t  throttle_enc;
    uint8_t  map;
    uint8_t  iat_enc;
    uint8_t  engine_load_enc;
    uint16_t battery_cv;
    uint16_t fuel_rate_x100;
    int8_t   stft;
    uint8_t  score;
} log_record_t;

typedef struct {
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t record_count;
} logger_stats_t;

esp_err_t     flash_logger_init(void);
esp_err_t     flash_logger_write(const relay_packet_t *pkt, uint16_t trip_id);
esp_err_t     flash_logger_clear(void);
logger_stats_t flash_logger_stats(void);
