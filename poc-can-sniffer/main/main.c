#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mdns.h"

static const char *TAG = "ADV350";

#define WIFI_SSID      CONFIG_WIFI_SSID
#define WIFI_PASS      CONFIG_WIFI_PASS
#define CAN_TX_GPIO    GPIO_NUM_26
#define CAN_RX_GPIO    GPIO_NUM_27

static volatile bool ota_in_progress = false;

// ── OTA HTML page ──

static const char ota_html[] =
    "<!DOCTYPE html><html><head><title>ADV350 OTA</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;text-align:center;padding:2em}"
    "h1{color:#333}input{margin:1em}button{padding:0.8em 2em;font-size:1.2em}</style></head>"
    "<body><h1>ADV350 OTA Update</h1>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin'><br>"
    "<button type='submit'>Upload Firmware</button></form>"
    "<p id='status'></p></body></html>";

// ── OTA Handlers ──

static esp_err_t ota_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, ota_html, strlen(ota_html));
}

static esp_err_t ota_update_handler(httpd_req_t *req)
{
    char buf[1024];
    int remaining = req->content_len;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    bool header_skipped = false;
    esp_err_t err;

    ESP_LOGI(TAG, "OTA: receiving %d bytes", remaining);
    ota_in_progress = true;

    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        ota_in_progress = false;
        return ESP_FAIL;
    }

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, sizeof(buf) < remaining ? sizeof(buf) : remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_abort(ota_handle);
            ota_in_progress = false;
            return ESP_FAIL;
        }

        char *body = buf;
        int body_len = recv_len;

        if (!header_skipped) {
            char *boundary_end = strstr(buf, "\r\n\r\n");
            if (boundary_end) {
                boundary_end += 4;
                body = boundary_end;
                body_len = recv_len - (boundary_end - buf);
                header_skipped = true;
            } else {
                remaining -= recv_len;
                continue;
            }
        }

        if (body_len > 0) {
            err = esp_ota_write(ota_handle, body, body_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
                esp_ota_abort(ota_handle);
                ota_in_progress = false;
                return ESP_FAIL;
            }
        }
        remaining -= recv_len;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA verify failed");
        ota_in_progress = false;
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_ota_set_boot_partition(update_partition));
    httpd_resp_sendstr(req, "OTA OK! Rebooting...");
    ESP_LOGI(TAG, "OTA complete, rebooting in 1s");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// ── WiFi ──

static volatile bool wifi_connected = false;
static esp_ip4_addr_t wifi_ip = {0};

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        wifi_ip = event->ip_info.ip;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&wifi_ip));
        wifi_connected = true;
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
}

// ── OTA Rollback Check ──

static void ota_rollback_check(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "New firmware - validating...");
        if (esp_get_free_heap_size() > 50000) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "Firmware validated OK");
        } else {
            ESP_LOGE(TAG, "Validation failed - rolling back");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }
}

// ── CAN Sniffer ──

static void can_sniffer_task(void *arg)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    twai_start();

    ESP_LOGI(TAG, "CAN sniffer started: 500kbps, ListenOnly, GPIO26/27");

    uint64_t frame_count = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t last_stats_us = start_us;

    while (1) {
        twai_message_t msg;
        err = twai_receive(&msg, pdMS_TO_TICKS(100));

        if (err == ESP_OK) {
            frame_count++;
            int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;

            printf("[%8lldms] #%-6llu ID:0x%03lX [%d] ",
                   elapsed_ms, frame_count,
                   (unsigned long)msg.identifier, msg.data_length_code);

            for (int i = 0; i < msg.data_length_code; i++) {
                printf("%02X ", msg.data[i]);
            }
            if (msg.extd) printf("(EXT)");
            if (msg.rtr) printf("(RTR)");
            printf("\n");
        }

        int64_t now = esp_timer_get_time();
        if ((now - last_stats_us) >= 10000000) {
            int64_t elapsed_s = (now - start_us) / 1000000;
            uint64_t fps = elapsed_s > 0 ? frame_count / elapsed_s : 0;
            ESP_LOGI(TAG, "--- STATS: %llu frames in %llds (%llu fps) ---",
                     frame_count, elapsed_s, fps);
            last_stats_us = now;
        }
    }
}

// ── Main ──

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ota_rollback_check();

    ESP_LOGI(TAG, "=== ADV350 CAN Sniffer + OTA ===");

    // Phase 1: WiFi + OTA window
    ESP_LOGI(TAG, "Connecting to WiFi '%s'...", WIFI_SSID);
    wifi_init_sta();

    // Wait for WiFi (max 10s)
    for (int i = 0; i < 20 && !wifi_connected; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (wifi_connected) {
        mdns_init();
        mdns_hostname_set("adv350");
        mdns_instance_name_set("ADV350 CAN Sniffer");
        ESP_LOGI(TAG, "mDNS: http://adv350.local");

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.recv_wait_timeout = 30;
        config.max_uri_handlers = 4;

        httpd_handle_t server = NULL;
        if (httpd_start(&server, &config) == ESP_OK) {
            httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = ota_page_handler };
            httpd_uri_t update = { .uri = "/update", .method = HTTP_POST, .handler = ota_update_handler };
            httpd_register_uri_handler(server, &page);
            httpd_register_uri_handler(server, &update);
            ESP_LOGI(TAG, "OTA web server running — upload anytime at http://" IPSTR, IP2STR(&wifi_ip));
        }
    } else {
        ESP_LOGW(TAG, "WiFi not connected - OTA unavailable, CAN sniffer only");
    }

    // Phase 2: CAN sniffer
    xTaskCreate(can_sniffer_task, "can_sniffer", 4096, NULL, 5, NULL);
}
