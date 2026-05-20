#include "espnow_rx.h"
#include <string.h>
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_timer.h"

static const char *TAG = "ENOW";

#define STALE_US  3000000  // 3 seconds

relay_state_t g_relay = {0};

bool relay_data_fresh(void)
{
    return g_relay.peer_known &&
           (esp_timer_get_time() - g_relay.last_rx_us) < STALE_US;
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(relay_packet_t)) return;

    const relay_packet_t *pkt = (const relay_packet_t *)data;
    if (pkt->magic != RELAY_MAGIC || pkt->version != RELAY_VERSION) return;

    // Track packet loss in callback (accurate)
    if (g_relay.rx_count > 0) {
        uint8_t expected = ((const relay_packet_t *)&g_relay.pkt)->seq + 1;
        if (pkt->seq != expected) {
            g_relay.rx_lost += (uint8_t)(pkt->seq - expected);
        }
    }

    memcpy(&g_relay.pkt, pkt, sizeof(relay_packet_t));
    g_relay.last_rx_us = esp_timer_get_time();
    g_relay.rx_count++;

    if (!g_relay.peer_known) {
        memcpy(g_relay.peer_mac, info->src_addr, 6);
        g_relay.peer_known = true;

        esp_now_peer_info_t peer = {
            .channel = 0,
            .encrypt = false,
        };
        memcpy(peer.peer_addr, info->src_addr, 6);
        esp_now_add_peer(&peer);

        ESP_LOGI(TAG, "Peer registered: %02X:%02X:%02X:%02X:%02X:%02X",
                 info->src_addr[0], info->src_addr[1], info->src_addr[2],
                 info->src_addr[3], info->src_addr[4], info->src_addr[5]);
    }
}

esp_err_t espnow_rx_init(void)
{
    // WiFi must be initialized before ESP-NOW
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Lock to channel 1 (must match sender)
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    ESP_LOGI(TAG, "ESP-NOW receiver ready (ch 1)");
    return ESP_OK;
}

esp_err_t relay_send_cmd(uint8_t cmd)
{
    if (!g_relay.peer_known) return ESP_ERR_INVALID_STATE;

    relay_cmd_t c = {
        .magic = RELAY_CMD_MAGIC,
        .cmd = cmd,
    };
    return esp_now_send(g_relay.peer_mac, (uint8_t *)&c, sizeof(c));
}
