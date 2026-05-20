#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "relay_packet.h"

typedef struct {
    relay_packet_t pkt;
    int64_t        last_rx_us;
    uint8_t        peer_mac[6];
    bool           peer_known;
    uint32_t       rx_count;
    uint32_t       rx_lost;
} relay_state_t;

extern relay_state_t g_relay;

esp_err_t espnow_rx_init(void);
esp_err_t relay_send_cmd(uint8_t cmd);
bool      relay_data_fresh(void);   // received within last 3 s
