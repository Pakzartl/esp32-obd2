#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "status_led.h"
#include "espnow_rx.h"
#include "ble_relay.h"
#include "smart_features.h"
#include "flash_logger.h"
#include "wifi_portal.h"

static const char *TAG = "RELAY";

static led_color_t connection_led_color(bool fresh)
{
    if (ble_is_connected()) {
        return LED_GREEN;
    }
    if (fresh || g_relay.peer_known) {
        return LED_ORANGE;
    }
    return LED_RED;
}

static void status_task(void *arg)
{
    uint32_t tick = 0;

    while (1) {
        bool fresh = relay_data_fresh();

        if (fresh) {
            const relay_packet_t *p = &g_relay.pkt;
            smart_features_update(p);

            if (g_trip.state == TRIP_ACTIVE && !ble_is_connected()) {
                flash_logger_write(p, (uint16_t)g_trip.trip_count);
            }

            status_led_set(connection_led_color(fresh));

            ESP_LOGI(TAG, "seq=%d RPM=%d SPD=%d CLT=%d THR=%d fuel=%.2f stft=%d lam=%d trip=%s",
                     p->seq,
                     (p->vd_flags & RF_RPM) ? p->vd_rpm : 0,
                     (p->vd_flags & RF_SPEED) ? p->vd_speed : 0,
                     (p->vd_flags & RF_COOLANT) ? p->vd_coolant_enc - 40 : -99,
                     (p->vd_flags & RF_THROTTLE) ? (p->vd_throttle_enc * 100 / 255) : -1,
                     p->vd_fuel_rate_x100 / 100.0f,
                     p->vd_stft,
                     p->vd_lambda_x1000,
                     g_trip.state == TRIP_ACTIVE ? "ACTIVE" : "idle");
        } else {
            status_led_set(connection_led_color(fresh));
        }

        tick++;
        if (tick % 15 == 0) {
            uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
            float loss_pct = g_relay.rx_count > 0
                ? (float)g_relay.rx_lost / (g_relay.rx_count + g_relay.rx_lost) * 100.0f
                : 0;
            float board_temp = smart_get_board_temp();
            ESP_LOGI(TAG, "[STATS] up=%lus heap=%lu rx=%lu lost=%lu (%.1f%%) board=%.1fC trip#%lu(%lus) peer=%s",
                     (unsigned long)uptime_s,
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)g_relay.rx_count,
                     (unsigned long)g_relay.rx_lost,
                     loss_pct,
                     board_temp,
                     (unsigned long)g_trip.trip_count,
                     (unsigned long)g_trip.active_seconds,
                     g_relay.peer_known ? "yes" : "no");
        }

        vTaskDelay(pdMS_TO_TICKS(fresh ? 2000 : 500));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI(TAG, "=== ADV350 BLE Relay (ESP32-S3) %s ===", FW_VERSION_S3);
    ESP_LOGI(TAG, "Free heap: %lu", (unsigned long)esp_get_free_heap_size());

    status_led_init();
    status_led_set(LED_RED);
    smart_features_init();
    flash_logger_init();
    espnow_rx_init();
    wifi_portal_init();
    ble_relay_init();

    ESP_LOGI(TAG, "Waiting for ESP-NOW data from CAN board...");
    ESP_LOGI(TAG, "Free heap after init: %lu", (unsigned long)esp_get_free_heap_size());

    xTaskCreate(status_task, "status", 4096, NULL, 2, NULL);
}
