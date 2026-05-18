#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mdns.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_store.h"

static const char *TAG = "ADV350";

#define WIFI_SSID      CONFIG_WIFI_SSID
#define WIFI_PASS      CONFIG_WIFI_PASS
#define CAN_TX_GPIO    GPIO_NUM_26
#define CAN_RX_GPIO    GPIO_NUM_27
#define MODE_BTN_GPIO  GPIO_NUM_0
#define MODE_HOLD_MS   5000

typedef enum { MODE_DEV, MODE_PROD } app_mode_t;

static app_mode_t current_mode;
static volatile bool ota_in_progress = false;

// TODO: ble_notify_can_frame will be re-enabled when GATT service is fixed

// ── Mode Detection ──

static app_mode_t get_default_mode(void)
{
#ifdef CONFIG_DEFAULT_MODE_PROD
    return MODE_PROD;
#else
    return MODE_DEV;
#endif
}

static bool is_button_held(int ms)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << MODE_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&cfg);

    int held = 0;
    while (held < ms) {
        if (gpio_get_level(MODE_BTN_GPIO) == 0) {
            held += 50;
        } else {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;
}

// ── Mode Switch via Button Task ──

static void mode_switch_task(void *arg)
{
    while (1) {
        if (gpio_get_level(MODE_BTN_GPIO) == 0) {
            ESP_LOGI(TAG, "IO0 pressed — hold 5s to switch mode...");
            if (is_button_held(MODE_HOLD_MS)) {
                app_mode_t new_mode = (current_mode == MODE_DEV) ? MODE_PROD : MODE_DEV;
                ESP_LOGI(TAG, "Switching to %s mode — rebooting...",
                         new_mode == MODE_DEV ? "DEV" : "PROD");

                nvs_handle_t nvs;
                if (nvs_open("config", NVS_READWRITE, &nvs) == ESP_OK) {
                    nvs_set_u8(nvs, "mode", (uint8_t)new_mode);
                    nvs_commit(nvs);
                    nvs_close(nvs);
                }

                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static app_mode_t load_mode(void)
{
    nvs_handle_t nvs;
    uint8_t mode_val;
    if (nvs_open("config", NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_u8(nvs, "mode", &mode_val) == ESP_OK) {
            nvs_close(nvs);
            return (mode_val == MODE_PROD) ? MODE_PROD : MODE_DEV;
        }
        nvs_close(nvs);
    }
    return get_default_mode();
}

// ── OTA ──

static const char ota_html[] =
    "<!DOCTYPE html><html><head><title>ADV350 OTA</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;text-align:center;padding:2em}"
    "h1{color:#333}input{margin:1em}button{padding:0.8em 2em;font-size:1.2em}"
    ".mode{background:#e0f0ff;padding:0.5em;border-radius:8px;margin:1em}</style></head>"
    "<body><h1>ADV350 OTA Update</h1>"
    "<div class='mode'>Mode: DEV (WiFi + OTA)</div>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin'><br>"
    "<button type='submit'>Upload Firmware</button></form>"
    "<p>Hold IO0 button 5s to switch to PROD mode</p></body></html>";

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

// ── OTA Rollback ──

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

            // TODO: BLE notify when GATT service is fixed
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

// ── DEV Mode (WiFi + OTA + CAN) ──

static void start_dev_mode(void)
{
    ESP_LOGI(TAG, "=== DEV MODE (WiFi + OTA + CAN) ===");

    ESP_LOGI(TAG, "Connecting to WiFi '%s'...", WIFI_SSID);
    wifi_init_sta();

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
            ESP_LOGI(TAG, "OTA web server running at http://" IPSTR, IP2STR(&wifi_ip));
        }
    } else {
        ESP_LOGW(TAG, "WiFi not connected - OTA unavailable");
    }

    xTaskCreate(can_sniffer_task, "can_sniffer", 4096, NULL, 5, NULL);
}

// ── BLE (PROD mode only — no WiFi) ──

static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;

#define BLE_DEVICE_NAME "ADV350"

static void ble_advertise(void);

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ble_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE: connected");
        } else {
            ble_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        // TODO: ble_live_notify = false;
        ESP_LOGI(TAG, "BLE: disconnected");
        ble_advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "BLE: subscribe event");
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "BLE: MTU=%d", event->mtu.value);
        break;
    }
    return 0;
}

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)BLE_DEVICE_NAME;
    fields.name_len = strlen(BLE_DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    ble_gap_adv_set_fields(&fields);

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE: on_sync called, starting advertise...");
    ble_advertise();
    ESP_LOGI(TAG, "BLE: advertising as '%s'", BLE_DEVICE_NAME);
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_init(void)
{
    ESP_LOGI(TAG, "BLE: nimble_port_init...");
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE: nimble_port_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "BLE: nimble_port_init OK");

    ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_LOGI(TAG, "BLE: GAP+GATT init OK");

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ESP_LOGI(TAG, "BLE: starting host task...");
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE: host task started");
}

// TODO: ble_notify_can_frame — re-enable when GATT service is fixed

// ── PROD Mode (BLE + CAN) ──

static void start_prod_mode(void)
{
    ESP_LOGI(TAG, "=== PROD MODE (BLE + CAN) ===");

    ESP_LOGI(TAG, "Free heap before BLE: %lu", (unsigned long)esp_get_free_heap_size());
    ble_init();
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Free heap after BLE: %lu", (unsigned long)esp_get_free_heap_size());

    xTaskCreate(can_sniffer_task, "can_sniffer", 4096, NULL, 5, NULL);
}

// ── Main ──

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

    ota_rollback_check();

    current_mode = load_mode();
    ESP_LOGI(TAG, "Boot mode: %s (hold IO0 5s to switch)",
             current_mode == MODE_DEV ? "DEV" : "PROD");

    xTaskCreate(mode_switch_task, "mode_sw", 2048, NULL, 10, NULL);

    if (current_mode == MODE_DEV) {
        start_dev_mode();
    } else {
        start_prod_mode();
    }
}
