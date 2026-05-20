#include "espnow_tx.h"
#include "relay_packet.h"
#include "obd2.h"
#include "metrics.h"
#include <string.h>
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ENOW-TX";

static const uint8_t s3_mac[6] = {0xE0, 0x72, 0xA1, 0xD6, 0xBF, 0x94};

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < 2 || data[0] != RELAY_CMD_MAGIC) return;

    uint8_t cmd = data[1];
    ESP_LOGI(TAG, "Relay cmd: 0x%02X", cmd);

    switch (cmd) {
    case 0x12:
        obd2_read_dtcs();
        break;
    case 0x13:
        metrics_reset_trip();
        break;
    }
}

static void build_packet(relay_packet_t *pkt)
{
    static uint8_t seq = 0;
    memset(pkt, 0, sizeof(*pkt));
    pkt->magic = RELAY_MAGIC;
    pkt->version = RELAY_VERSION;
    pkt->seq = seq++;

    uint16_t flags = 0;
    float v;

    v = vd_rpm(&g_vehicle);
    if (v >= 0) { flags |= RF_RPM; pkt->vd_rpm = (uint16_t)v; }

    v = vd_speed(&g_vehicle);
    if (v >= 0) { flags |= RF_SPEED; pkt->vd_speed = (uint8_t)v; }

    v = vd_coolant(&g_vehicle);
    if (v > -100) { flags |= RF_COOLANT; pkt->vd_coolant_enc = (uint8_t)(v + 40); }

    v = vd_throttle(&g_vehicle);
    if (v >= 0) { flags |= RF_THROTTLE; pkt->vd_throttle_enc = (uint8_t)(v * 2.55f); }

    v = vd_map(&g_vehicle);
    if (v >= 0) { flags |= RF_MAP; pkt->vd_map = (uint8_t)v; }

    v = vd_iat(&g_vehicle);
    if (v > -100) { flags |= RF_IAT; pkt->vd_iat_enc = (uint8_t)(v + 40); }

    if (sensor_fresh(&g_vehicle.battery_v)) {
        flags |= RF_BATTERY;
        pkt->vd_battery_cv = (uint16_t)(g_vehicle.battery_v.value * 100);
    }

    if (g_metrics.valid) {
        flags |= RF_METRICS;
        pkt->vd_fuel_rate_x100 = (uint16_t)(g_metrics.fuel_rate_lph * 100);
        pkt->vd_cvt_x100 = (uint16_t)(g_metrics.cvt_ratio * 100);
        pkt->vd_score = g_metrics.riding_score;
        pkt->metrics_valid = 1;
        pkt->mt_accel_x100 = (int16_t)(g_metrics.accel_ms2 * 100);
        pkt->mt_gforce_x1000 = (int16_t)(g_metrics.gforce_x * 1000);
        pkt->mt_fuel_kmpl_x10 = (int16_t)(g_metrics.fuel_consumption_kmpl * 10);
        pkt->mt_trip_fuel_x1000 = (int16_t)(g_metrics.trip_fuel_l * 1000);
        pkt->mt_trip_dist_x100 = (int16_t)(g_metrics.trip_dist_km * 100);
        pkt->mt_trip_avg_x10 = (int16_t)(g_metrics.trip_avg_kmpl * 10);
        pkt->mt_power_x100 = (int16_t)(g_metrics.wheel_power_kw * 100);
        pkt->mt_brake_dist_x10 = (int16_t)(g_metrics.braking_dist_m * 10);
    }

    pkt->vd_flags = flags;
}

esp_err_t espnow_tx_init(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    esp_now_peer_info_t peer = {
        .channel = 0,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, s3_mac, 6);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "ESP-NOW sender ready (ch 1, broadcast)");
    return ESP_OK;
}

void espnow_tx_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "Sender task started (10 Hz)");

    while (1) {
        relay_packet_t pkt;
        build_packet(&pkt);
        esp_now_send(s3_mac, (uint8_t *)&pkt, sizeof(pkt));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
