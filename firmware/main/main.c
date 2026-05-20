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
#include "host/ble_gatt.h"
#include "obd2.h"
#include "metrics.h"
#include "espnow_tx.h"

#define FW_VERSION "0.3.0"

static const char *TAG = "ADV350";

#define WIFI_SSID      CONFIG_WIFI_SSID
#define WIFI_PASS      CONFIG_WIFI_PASS
#define CAN_TX_GPIO    GPIO_NUM_26
#define CAN_RX_GPIO    GPIO_NUM_27
#define MODE_BTN_GPIO  GPIO_NUM_0
#define MODE_HOLD_MS   5000
#define LED_GPIO       GPIO_NUM_5

typedef enum { MODE_DEV, MODE_PROD } app_mode_t;

static app_mode_t current_mode;
static volatile bool ota_in_progress = false;

// ── CAN Frame Ring Buffer (for WiFi log viewer) ──

#define LOG_RING_SIZE 100

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    int64_t timestamp_ms;
    uint32_t seq;
} log_frame_t;

static log_frame_t s_log_ring[LOG_RING_SIZE];
static volatile int s_log_head = 0;
static volatile int s_log_count = 0;
static volatile uint32_t s_log_seq = 0;
static volatile uint64_t s_total_frames = 0;
static volatile uint64_t s_total_fps = 0;

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

                for (int i = 0; i < 2; i++) {
                    gpio_set_level(LED_GPIO, 0);
                    vTaskDelay(pdMS_TO_TICKS(30));
                    gpio_set_level(LED_GPIO, 1);
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                vTaskDelay(pdMS_TO_TICKS(200));
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

static void ble_notify_can_frame(const twai_message_t *msg);

// ── CAN Diagnostic: Self-test + Bitrate Scan + Honda Wake-up ──

static volatile bool diag_done = false;
static char diag_result[512] = "Not run yet";

static void can_diag_task(void *arg)
{
    int pos = 0;
    pos += snprintf(diag_result + pos, sizeof(diag_result) - pos, "=== CAN DIAG ===\n");

    // Step 1: Self-test (loopback, no external bus needed)
    {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NO_ACK);
        twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
        twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        esp_err_t err = twai_driver_install(&g, &t, &f);
        if (err == ESP_OK) {
            twai_start();
            twai_message_t tx = {.identifier = 0x123, .data_length_code = 4,
                                 .data = {0xDE, 0xAD, 0xBE, 0xEF}};
            err = twai_transmit(&tx, pdMS_TO_TICKS(100));
            twai_message_t rx;
            esp_err_t rx_err = twai_receive(&rx, pdMS_TO_TICKS(200));
            if (err == ESP_OK && rx_err == ESP_OK && rx.identifier == 0x123) {
                pos += snprintf(diag_result + pos, sizeof(diag_result) - pos,
                    "SELF-TEST: PASS (TX+RX loopback OK)\n");
            } else {
                pos += snprintf(diag_result + pos, sizeof(diag_result) - pos,
                    "SELF-TEST: FAIL (tx=%s rx=%s)\n", esp_err_to_name(err), esp_err_to_name(rx_err));
            }
            twai_stop();
            twai_driver_uninstall();
        } else {
            pos += snprintf(diag_result + pos, sizeof(diag_result) - pos,
                "SELF-TEST: INSTALL FAIL %s\n", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Step 2: Bitrate scan (listen-only, count bus errors per speed)
    uint32_t bitrates[] = {125, 250, 500, 1000};
    twai_timing_config_t timings[] = {
        TWAI_TIMING_CONFIG_125KBITS(),
        TWAI_TIMING_CONFIG_250KBITS(),
        TWAI_TIMING_CONFIG_500KBITS(),
        TWAI_TIMING_CONFIG_1MBITS(),
    };

    for (int i = 0; i < 4; i++) {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_LISTEN_ONLY);
        twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        if (twai_driver_install(&g, &timings[i], &f) != ESP_OK) continue;
        twai_start();
        vTaskDelay(pdMS_TO_TICKS(3000)); // listen 3 seconds

        twai_status_info_t st;
        twai_get_status_info(&st);

        twai_message_t rx;
        int rx_count = 0;
        while (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK) rx_count++;

        pos += snprintf(diag_result + pos, sizeof(diag_result) - pos,
            "%lukbps: frames=%d buserr=%lu rxerr=%lu\n",
            (unsigned long)bitrates[i], rx_count,
            (unsigned long)st.bus_error_count, (unsigned long)st.rx_error_counter);

        twai_stop();
        twai_driver_uninstall();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Step 3: Honda wake-up attempts at 500kbps NORMAL mode
    {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
        twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
        twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        if (twai_driver_install(&g, &t, &f) == ESP_OK) {
            twai_start();

            struct { uint32_t id; bool extd; uint8_t data[8]; uint8_t len; const char *name; } probes[] = {
                {0x7DF, false, {0x02, 0x01, 0x00, 0, 0, 0, 0, 0}, 8, "OBD2-7DF"},
                {0x7E0, false, {0x02, 0x01, 0x00, 0, 0, 0, 0, 0}, 8, "OBD2-7E0"},
                {0x7E0, false, {0x02, 0x10, 0x01, 0, 0, 0, 0, 0}, 8, "UDS-DiagSess"},
                {0x18DB33F1, true, {0x02, 0x01, 0x00, 0, 0, 0, 0, 0}, 8, "HDS-29bit"},
                {0x18DA10F1, true, {0x02, 0x10, 0x01, 0, 0, 0, 0, 0}, 8, "UDS-29bit"},
            };

            for (int i = 0; i < 5; i++) {
                twai_message_t tx = {
                    .identifier = probes[i].id,
                    .extd = probes[i].extd,
                    .data_length_code = probes[i].len,
                };
                memcpy(tx.data, probes[i].data, probes[i].len);

                esp_err_t tx_err = twai_transmit(&tx, pdMS_TO_TICKS(200));
                twai_message_t rx;
                esp_err_t rx_err = twai_receive(&rx, pdMS_TO_TICKS(500));

                pos += snprintf(diag_result + pos, sizeof(diag_result) - pos,
                    "%s: tx=%s rx=%s", probes[i].name,
                    tx_err == ESP_OK ? "OK" : "FAIL",
                    rx_err == ESP_OK ? "OK" : "TIMEOUT");
                if (rx_err == ESP_OK) {
                    pos += snprintf(diag_result + pos, sizeof(diag_result) - pos,
                        " ID:0x%lX [%d]", (unsigned long)rx.identifier, rx.data_length_code);
                }
                pos += snprintf(diag_result + pos, sizeof(diag_result) - pos, "\n");
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            twai_stop();
            twai_driver_uninstall();
        }
    }

    pos += snprintf(diag_result + pos, sizeof(diag_result) - pos, "=== DONE ===\n");
    ESP_LOGI(TAG, "%s", diag_result);
    diag_done = true;
    vTaskDelete(NULL);
}

// HTTP handler for /api/diag
static esp_err_t api_diag_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, diag_result, strlen(diag_result));
}

// ── DID Brute-force Scanner ──

#define SCAN_RESULT_SIZE 4096
static char scan_result[SCAN_RESULT_SIZE] = "Not scanned yet. GET /api/scan?range=f4 to start.";
static volatile bool scan_running = false;
volatile bool scan_pause_others = false;  // pause sniffer + poll during scan

static void did_scan_task(void *arg)
{
    uint16_t range_start = (uint16_t)(uintptr_t)arg;
    uint16_t range_end = range_start + 0xFF;

    scan_running = true;
    scan_pause_others = true;
    vTaskDelay(pdMS_TO_TICKS(500)); // let other tasks drain

    // Drain any pending RX frames
    twai_message_t drain;
    while (twai_receive(&drain, pdMS_TO_TICKS(50)) == ESP_OK) {}

    // Try extended session first (needed for proprietary DIDs)
    {
        uint8_t ext_sess[] = {0x02, UDS_DIAG_SESSION, 0x03, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        twai_message_t tx = {.identifier = HONDA_UDS_REQUEST_ID, .extd = 1, .data_length_code = 8};
        memcpy(tx.data, ext_sess, 8);
        twai_transmit(&tx, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(200));
        while (twai_receive(&drain, pdMS_TO_TICKS(50)) == ESP_OK) {}
    }

    int pos = 0;
    int found = 0;
    pos += snprintf(scan_result + pos, SCAN_RESULT_SIZE - pos,
        "=== DID SCAN 0x%04X-0x%04X ===\n", range_start, range_end);

    for (uint16_t did = range_start; did <= range_end && pos < SCAN_RESULT_SIZE - 120; did++) {
        // Send ReadDataByIdentifier
        uint8_t req[] = {0x03, UDS_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
                         0xAA, 0xAA, 0xAA, 0xAA};
        twai_message_t tx = {.identifier = HONDA_UDS_REQUEST_ID, .extd = 1, .data_length_code = 8};
        memcpy(tx.data, req, 8);
        twai_transmit(&tx, pdMS_TO_TICKS(100));

        twai_message_t rx;
        if (twai_receive(&rx, pdMS_TO_TICKS(150)) == ESP_OK) {
            if (rx.extd && rx.identifier == HONDA_UDS_RESPONSE_ID) {
                uint8_t pci_len = rx.data[0] & 0x0F;
                uint8_t sid = rx.data[1];
                if (sid == 0x62 && pci_len >= 3) {
                    uint16_t resp_did = ((uint16_t)rx.data[2] << 8) | rx.data[3];
                    pos += snprintf(scan_result + pos, SCAN_RESULT_SIZE - pos,
                        "0x%04X: ", resp_did);
                    for (int b = 4; b < 1 + pci_len && b < 8; b++) {
                        pos += snprintf(scan_result + pos, SCAN_RESULT_SIZE - pos,
                            "%02X ", rx.data[b]);
                    }
                    pos += snprintf(scan_result + pos, SCAN_RESULT_SIZE - pos, "\n");
                    found++;
                } else if (sid == 0x7F && pci_len >= 3 && rx.data[3] == 0x22) {
                    // NRC conditionsNotCorrect — DID exists but needs different session
                    pos += snprintf(scan_result + pos, SCAN_RESULT_SIZE - pos,
                        "0x%04X: LOCKED (needs auth)\n", did);
                    found++;
                }
            }
        }
    }
    pos += snprintf(scan_result + pos, SCAN_RESULT_SIZE - pos,
        "=== DONE: %d DIDs found ===\n", found);
    ESP_LOGI(TAG, "DID scan complete: %d found in 0x%04X-0x%04X", found, range_start, range_end);

    scan_pause_others = false;
    scan_running = false;
    vTaskDelete(NULL);
}

// /api/scan?range=xx       → view result (or "not scanned yet")
// /api/scan?range=xx&go=1  → start new scan
static esp_err_t api_scan_handler(httpd_req_t *req)
{
    char query[48] = {0};
    uint16_t range = 0xF400;
    bool start_new = false;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "range", val, sizeof(val)) == ESP_OK) {
            unsigned int r = 0;
            if (sscanf(val, "%x", &r) == 1) range = (uint16_t)(r << 8);
        }
        if (httpd_query_key_value(query, "go", val, sizeof(val)) == ESP_OK) {
            start_new = true;
        }
    }

    if (start_new && !scan_running) {
        xTaskCreatePinnedToCore(did_scan_task, "did_scan", 4096,
            (void *)(uintptr_t)range, 4, NULL, 1);
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (scan_running) {
        httpd_resp_sendstr_chunk(req, "[SCANNING...]\n");
        httpd_resp_sendstr_chunk(req, scan_result);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }
    return httpd_resp_send(req, scan_result, strlen(scan_result));
}

// ── CAN Sniffer ──

static void can_sniffer_task(void *arg)
{
    // Run diagnostics only in DEV mode (PROD already knows the protocol)
    if (current_mode == MODE_DEV) {
        ESP_LOGI(TAG, "Running CAN diagnostics (DEV mode)...");
        diag_done = false;
        xTaskCreatePinnedToCore(can_diag_task, "can_diag", 4096, NULL, 5, NULL, 1);
        while (!diag_done) vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "CAN diagnostics complete");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    twai_start();

    ESP_LOGI(TAG, "CAN sniffer started: 500kbps, Normal, GPIO26/27");

    uint64_t frame_count = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t last_stats_us = start_us;

    while (1) {
        if (scan_pause_others) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }

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

            ble_notify_can_frame(&msg);
            obd2_process_frame(&msg);

            // Push to ring buffer for WiFi log viewer
            log_frame_t *lf = &s_log_ring[s_log_head];
            lf->id = msg.identifier;
            lf->dlc = msg.data_length_code;
            memcpy(lf->data, msg.data, msg.data_length_code);
            lf->timestamp_ms = elapsed_ms;
            lf->seq = ++s_log_seq;
            s_log_head = (s_log_head + 1) % LOG_RING_SIZE;
            if (s_log_count < LOG_RING_SIZE) s_log_count++;
        }

        int64_t now = esp_timer_get_time();
        if ((now - last_stats_us) >= 10000000) {
            int64_t elapsed_s = (now - start_us) / 1000000;
            uint64_t fps = elapsed_s > 0 ? frame_count / elapsed_s : 0;
            s_total_frames = frame_count;
            s_total_fps = fps;
            ESP_LOGI(TAG, "--- STATS: %llu frames in %llds (%llu fps) ---",
                     frame_count, elapsed_s, fps);
            last_stats_us = now;
        }
    }
}

// ── DEV Mode (WiFi + OTA + CAN) ──

// ── WiFi CAN Log Viewer ──

static const char log_html[] =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{background:#111;color:#eee;font-family:monospace;margin:0;padding:8px}"
    "h1{font-size:16px;color:#4fc3f7;margin:4px 0}"
    "table{width:100%;border-collapse:collapse;font-size:12px}"
    "th{background:#222;color:#888;text-align:left;padding:4px;position:sticky;top:0}"
    "td{padding:2px 4px;border-bottom:1px solid #222}"
    ".i{color:#ffa726}.d{color:#66bb6a}.s{color:#888;font-size:11px;margin:4px 0}"
    "a{color:#4fc3f7}"
    "</style></head><body>"
    "<h1>ADV350 CAN Log</h1>"
    "<div class='s' id='st'>Connecting...</div>"
    "<table><thead><tr><th>ms</th><th>ID</th><th>DLC</th><th>DATA</th></tr></thead>"
    "<tbody id='tb'></tbody></table>"
    "<script>"
    "let sq=0;"
    "async function p(){"
    "try{"
    "const r=await fetch('/api/frames?since='+sq);"
    "const j=await r.json();"
    "if(j.f.length>0){"
    "const t=document.getElementById('tb');"
    "j.f.forEach(f=>{"
    "const r=document.createElement('tr');"
    "r.innerHTML='<td>'+f.t+'</td><td class=i>0x'+f.i.toString(16).toUpperCase().padStart(3,'0')+'</td><td>'+f.d+'</td><td class=d>'+f.b+'</td>';"
    "t.appendChild(r);"
    "sq=Math.max(sq,f.s);"
    "});"
    "while(t.rows.length>300)t.deleteRow(0);"
    "window.scrollTo(0,document.body.scrollHeight);"
    "}"
    "var t=j.twai||{};"
    "document.getElementById('st').textContent="
    "'Frames: '+j.n+' | FPS: '+j.p+' | Heap: '+j.h+' | Ring: '+j.f.length"
    "+(t.state!=undefined?' | TWAI:'+['STOPPED','RUNNING','BUS_OFF','RECOVERING'][t.state]+' TXerr:'+t.tx_err+' RXerr:'+t.rx_err+' TXfail:'+t.tx_fail+' BUSerr:'+t.bus_err:'');"
    "}catch(e){document.getElementById('st').textContent='Error: '+e;}"
    "}"
    "setInterval(p,200);"
    "</script></body></html>";

static esp_err_t log_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, log_html, strlen(log_html));
}

static esp_err_t api_frames_handler(httpd_req_t *req)
{
    uint32_t since = 0;
    char query[32] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "since", val, sizeof(val)) == ESP_OK) {
            since = (uint32_t)atoi(val);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // TWAI bus status for remote debug
    twai_status_info_t twai_status;
    twai_get_status_info(&twai_status);

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"n\":%llu,\"p\":%llu,\"h\":%lu,"
        "\"twai\":{\"state\":%d,\"tx_err\":%lu,\"rx_err\":%lu,\"tx_fail\":%lu,\"rx_miss\":%lu,\"arb_lost\":%lu,\"bus_err\":%lu},"
        "\"f\":[",
        s_total_frames, s_total_fps, (unsigned long)esp_get_free_heap_size(),
        (int)twai_status.state,
        (unsigned long)twai_status.tx_error_counter,
        (unsigned long)twai_status.rx_error_counter,
        (unsigned long)twai_status.tx_failed_count,
        (unsigned long)twai_status.rx_missed_count,
        (unsigned long)twai_status.arb_lost_count,
        (unsigned long)twai_status.bus_error_count);
    httpd_resp_send_chunk(req, buf, len);

    int start = (s_log_count < LOG_RING_SIZE) ? 0 : s_log_head;
    int count = s_log_count;
    bool first = true;

    for (int i = 0; i < count; i++) {
        int idx = (start + i) % LOG_RING_SIZE;
        log_frame_t *f = &s_log_ring[idx];
        if (f->seq <= since) continue;

        char hex[32] = {0};
        int hpos = 0;
        for (int b = 0; b < f->dlc && b < 8; b++) {
            hpos += snprintf(hex + hpos, sizeof(hex) - hpos, "%s%02X", b ? " " : "", f->data[b]);
        }

        len = snprintf(buf, sizeof(buf), "%s{\"t\":%lld,\"i\":%lu,\"d\":%d,\"b\":\"%s\",\"s\":%lu}",
            first ? "" : ",",
            f->timestamp_ms, (unsigned long)f->id, f->dlc, hex, (unsigned long)f->seq);
        httpd_resp_send_chunk(req, buf, len);
        first = false;
    }

    httpd_resp_send_chunk(req, "]}", 2);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static void start_dev_mode(void)
{
    ESP_LOGI(TAG, "=== DEV MODE (WiFi + OTA + CAN + OBD2 + Metrics) ===");
    obd2_init();
    metrics_init();

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
        config.max_uri_handlers = 8;

        httpd_handle_t server = NULL;
        if (httpd_start(&server, &config) == ESP_OK) {
            httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = ota_page_handler };
            httpd_uri_t update = { .uri = "/update", .method = HTTP_POST, .handler = ota_update_handler };
            httpd_uri_t logpage = { .uri = "/log", .method = HTTP_GET, .handler = log_page_handler };
            httpd_uri_t apiframes = { .uri = "/api/frames", .method = HTTP_GET, .handler = api_frames_handler };
            httpd_register_uri_handler(server, &page);
            httpd_register_uri_handler(server, &update);
            httpd_uri_t apidiag = { .uri = "/api/diag", .method = HTTP_GET, .handler = api_diag_handler };
            httpd_register_uri_handler(server, &logpage);
            httpd_register_uri_handler(server, &apiframes);
            httpd_uri_t apiscan = { .uri = "/api/scan", .method = HTTP_GET, .handler = api_scan_handler };
            httpd_register_uri_handler(server, &apidiag);
            httpd_register_uri_handler(server, &apiscan);
            ESP_LOGI(TAG, "OTA: http://" IPSTR, IP2STR(&wifi_ip));
            ESP_LOGI(TAG, "CAN Log: http://" IPSTR "/log", IP2STR(&wifi_ip));
            ESP_LOGI(TAG, "DID Scan: http://" IPSTR "/api/scan?range=f4", IP2STR(&wifi_ip));
        }
    } else {
        ESP_LOGW(TAG, "WiFi not connected - OTA unavailable");
    }

    xTaskCreatePinnedToCore(can_sniffer_task, "can_sniffer", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(obd2_poll_task, "obd2_poll", 4096, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "UDS polling auto-started (DEV mode, 29-bit extended)");
}

// ── BLE (PROD mode only — no WiFi) ──

static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;

#define BLE_DEVICE_NAME "ADV350"

// ── GATT CAN Service ──

static const ble_uuid128_t can_svc_uuid =
    BLE_UUID128_INIT(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t can_frame_chr_uuid =
    BLE_UUID128_INIT(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t can_ctrl_chr_uuid =
    BLE_UUID128_INIT(0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t vehicle_data_chr_uuid =
    BLE_UUID128_INIT(0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t metrics_chr_uuid =
    BLE_UUID128_INIT(0xf4, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t dtc_chr_uuid =
    BLE_UUID128_INIT(0xf5, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t ota_data_chr_uuid =
    BLE_UUID128_INIT(0xf6, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t fw_version_chr_uuid =
    BLE_UUID128_INIT(0xf7, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static uint16_t can_frame_chr_handle;
static uint16_t vehicle_data_chr_handle;
static uint16_t metrics_chr_handle;
static uint16_t dtc_chr_handle;
static uint16_t ota_data_chr_handle;
static volatile bool ble_live_notify = false;
static volatile bool ble_data_notify = false;
static volatile bool obd2_polling_active = false;

static esp_ota_handle_t ble_ota_handle = 0;
static const esp_partition_t *ble_ota_partition = NULL;
static uint32_t ble_ota_total = 0;
static uint32_t ble_ota_received = 0;
static volatile bool ble_ota_active = false;
static volatile uint8_t pending_led_blink = 0;
static TaskHandle_t obd2_poll_task_handle = NULL;

static int can_frame_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t status = ble_live_notify ? 1 : 0;
        os_mbuf_append(ctxt->om, &status, sizeof(status));
    }
    return 0;
}

// Vehicle data BLE packet: [flags:2][rpm:2][speed:1][coolant:1][throttle:1][map:1][iat:1][batt:2][fuel_rate:2][cvt:2][score:1]
static int vehicle_data_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return 0;

    uint8_t buf[16] = {0};
    uint16_t flags = 0;
    float v;

    v = vd_rpm(&g_vehicle);
    if (v >= 0) { flags |= (1 << 0); uint16_t r = (uint16_t)v; memcpy(&buf[2], &r, 2); }
    v = vd_speed(&g_vehicle);
    if (v >= 0) { flags |= (1 << 1); buf[4] = (uint8_t)v; }
    v = vd_coolant(&g_vehicle);
    if (v > -100) { flags |= (1 << 2); buf[5] = (uint8_t)(v + 40); }
    v = vd_throttle(&g_vehicle);
    if (v >= 0) { flags |= (1 << 3); buf[6] = (uint8_t)(v * 2.55f); }
    v = vd_map(&g_vehicle);
    if (v >= 0) { flags |= (1 << 4); buf[7] = (uint8_t)v; }
    v = vd_iat(&g_vehicle);
    if (v > -100) { flags |= (1 << 5); buf[8] = (uint8_t)(v + 40); }
    if (sensor_fresh(&g_vehicle.battery_v)) {
        flags |= (1 << 6);
        uint16_t bv = (uint16_t)(g_vehicle.battery_v.value * 100);
        memcpy(&buf[9], &bv, 2);
    }
    if (g_metrics.valid) {
        flags |= (1 << 7);
        uint16_t fr = (uint16_t)(g_metrics.fuel_rate_lph * 100);
        memcpy(&buf[11], &fr, 2);
        uint16_t cvt = (uint16_t)(g_metrics.cvt_ratio * 100);
        memcpy(&buf[13], &cvt, 2);
        buf[15] = g_metrics.riding_score;
    }
    memcpy(buf, &flags, 2);
    os_mbuf_append(ctxt->om, buf, 16);
    return 0;
}

// Metrics BLE packet: [accel:2][gforce:2][fuel_kmpl:2][trip_fuel:2][trip_dist:2][trip_avg:2][power:2][brake_dist:2]
static int metrics_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return 0;

    int16_t buf[8] = {0};
    if (g_metrics.valid) {
        buf[0] = (int16_t)(g_metrics.accel_ms2 * 100);
        buf[1] = (int16_t)(g_metrics.gforce_x * 1000);
        buf[2] = (int16_t)(g_metrics.fuel_consumption_kmpl * 10);
        buf[3] = (int16_t)(g_metrics.trip_fuel_l * 1000);
        buf[4] = (int16_t)(g_metrics.trip_dist_km * 100);
        buf[5] = (int16_t)(g_metrics.trip_avg_kmpl * 10);
        buf[6] = (int16_t)(g_metrics.wheel_power_kw * 100);
        buf[7] = (int16_t)(g_metrics.braking_dist_m * 10);
    }
    os_mbuf_append(ctxt->om, buf, sizeof(buf));
    return 0;
}

// DTC characteristic: [count:1][dtc1:5bytes_ascii]...[dtcN:5bytes_ascii]
static int dtc_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return 0;

    uint8_t count = g_vehicle.dtcs_valid ? g_vehicle.dtc_count : 0xFF;
    os_mbuf_append(ctxt->om, &count, 1);
    for (int i = 0; i < g_vehicle.dtc_count && g_vehicle.dtcs_valid; i++) {
        os_mbuf_append(ctxt->om, g_vehicle.dtcs[i].text, 5);
    }
    return 0;
}

static int fw_version_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        os_mbuf_append(ctxt->om, FW_VERSION, strlen(FW_VERSION));
    }
    return 0;
}

static int ota_data_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t buf[9];
        buf[0] = ble_ota_active ? 1 : 0;
        memcpy(&buf[1], &ble_ota_received, 4);
        memcpy(&buf[5], &ble_ota_total, 4);
        os_mbuf_append(ctxt->om, buf, sizeof(buf));
        return 0;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    if (!ble_ota_active) return BLE_ATT_ERR_INVALID_HANDLE;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t chunk[512];
    if (len > sizeof(chunk)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    os_mbuf_copydata(ctxt->om, 0, len, chunk);
    esp_err_t err = esp_ota_write(ble_ota_handle, chunk, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE OTA: write failed: %s", esp_err_to_name(err));
        esp_ota_abort(ble_ota_handle);
        ble_ota_active = false;
        return BLE_ATT_ERR_UNLIKELY;
    }
    ble_ota_received += len;
    return 0;
}

// Control: 0x00=stop notify, 0x01=start notify, 0x02=ping LED,
//          0x10=start OBD2 poll, 0x11=stop OBD2 poll, 0x12=read DTCs, 0x13=reset trip
//          0x20=begin BLE OTA (+4byte size), 0x21=finish OTA, 0x22=abort OTA
static int can_ctrl_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t status[3] = {
            ble_live_notify ? 1 : 0,
            obd2_polling_active ? 1 : 0,
            g_vehicle.dids_probed ? 1 : 0,
        };
        os_mbuf_append(ctxt->om, status, sizeof(status));
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t val;
        if (OS_MBUF_PKTLEN(ctxt->om) >= 1) {
            os_mbuf_copydata(ctxt->om, 0, 1, &val);
            switch (val) {
            case 0x00:
                ble_live_notify = false;
                ESP_LOGI(TAG, "BLE: CAN raw notify OFF");
                break;
            case 0x01:
                ble_live_notify = true;
                ESP_LOGI(TAG, "BLE: CAN raw notify ON");
                break;
            case 0x02:
                pending_led_blink = 3;
                ESP_LOGI(TAG, "BLE: ping LED");
                break;
            case 0x10:
                if (!obd2_polling_active) {
                    obd2_polling_active = true;
                    xTaskCreatePinnedToCore(obd2_poll_task, "obd2_poll", 3072, NULL, 4, &obd2_poll_task_handle, 1);
                    ESP_LOGI(TAG, "BLE: OBD2 polling started");
                }
                break;
            case 0x11:
                if (obd2_polling_active && obd2_poll_task_handle) {
                    obd2_polling_active = false;
                    vTaskDelete(obd2_poll_task_handle);
                    obd2_poll_task_handle = NULL;
                    ESP_LOGI(TAG, "BLE: OBD2 polling stopped");
                }
                break;
            case 0x12:
                obd2_read_dtcs();
                ESP_LOGI(TAG, "BLE: DTC read requested");
                break;
            case 0x13:
                metrics_reset_trip();
                ESP_LOGI(TAG, "BLE: trip reset");
                break;
            case 0x20: {
                if (OS_MBUF_PKTLEN(ctxt->om) >= 5) {
                    uint32_t fw_size;
                    os_mbuf_copydata(ctxt->om, 1, 4, &fw_size);
                    ble_ota_partition = esp_ota_get_next_update_partition(NULL);
                    if (!ble_ota_partition || fw_size > ble_ota_partition->size) {
                        ESP_LOGE(TAG, "BLE OTA: invalid partition or size %lu", (unsigned long)fw_size);
                        break;
                    }
                    esp_err_t e = esp_ota_begin(ble_ota_partition, OTA_WITH_SEQUENTIAL_WRITES, &ble_ota_handle);
                    if (e != ESP_OK) {
                        ESP_LOGE(TAG, "BLE OTA: begin failed: %s", esp_err_to_name(e));
                        break;
                    }
                    ble_ota_total = fw_size;
                    ble_ota_received = 0;
                    ble_ota_active = true;
                    if (obd2_polling_active && obd2_poll_task_handle) {
                        obd2_polling_active = false;
                        vTaskDelete(obd2_poll_task_handle);
                        obd2_poll_task_handle = NULL;
                    }
                    ESP_LOGI(TAG, "BLE OTA: started, expecting %lu bytes", (unsigned long)fw_size);
                }
                break;
            }
            case 0x21: {
                if (!ble_ota_active) break;
                esp_err_t e = esp_ota_end(ble_ota_handle);
                if (e != ESP_OK) {
                    ESP_LOGE(TAG, "BLE OTA: verify failed: %s", esp_err_to_name(e));
                    ble_ota_active = false;
                    break;
                }
                ESP_ERROR_CHECK(esp_ota_set_boot_partition(ble_ota_partition));
                ble_ota_active = false;
                ESP_LOGI(TAG, "BLE OTA: complete (%lu bytes), rebooting", (unsigned long)ble_ota_received);
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
                break;
            }
            case 0x22:
                if (ble_ota_active) {
                    esp_ota_abort(ble_ota_handle);
                    ble_ota_active = false;
                    ble_ota_received = 0;
                    ble_ota_total = 0;
                    ESP_LOGI(TAG, "BLE OTA: aborted");
                }
                break;
            }
        }
    }
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &can_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &can_frame_chr_uuid.u,
                .access_cb = can_frame_chr_access,
                .val_handle = &can_frame_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &can_ctrl_chr_uuid.u,
                .access_cb = can_ctrl_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &vehicle_data_chr_uuid.u,
                .access_cb = vehicle_data_chr_access,
                .val_handle = &vehicle_data_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &metrics_chr_uuid.u,
                .access_cb = metrics_chr_access,
                .val_handle = &metrics_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &dtc_chr_uuid.u,
                .access_cb = dtc_chr_access,
                .val_handle = &dtc_chr_handle,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &ota_data_chr_uuid.u,
                .access_cb = ota_data_chr_access,
                .val_handle = &ota_data_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &fw_version_chr_uuid.u,
                .access_cb = fw_version_chr_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    { 0 },
};

static void ble_notify_can_frame(const twai_message_t *msg)
{
    if (ble_conn_handle == BLE_HS_CONN_HANDLE_NONE || !ble_live_notify) return;

    uint8_t buf[13];
    uint32_t id = msg->identifier;
    memcpy(buf, &id, 4);
    buf[4] = msg->data_length_code;
    memcpy(&buf[5], msg->data, msg->data_length_code);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, 5 + msg->data_length_code);
    if (om) {
        ble_gatts_notify_custom(ble_conn_handle, can_frame_chr_handle, om);
    }
}

static void ble_advertise(void);
static void ble_reboot_task(void *arg);

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
        ble_live_notify = false;
        ble_data_notify = false;
        ESP_LOGI(TAG, "BLE: disconnected, reason=%d", event->disconnect.reason);
        xTaskCreate(ble_reboot_task, "ble_reboot", 2048, NULL, 1, NULL);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == can_frame_chr_handle) {
            ble_live_notify = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "BLE: CAN raw notify %s", ble_live_notify ? "ON" : "OFF");
        } else if (event->subscribe.attr_handle == vehicle_data_chr_handle) {
            ble_data_notify = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "BLE: vehicle data notify %s", ble_data_notify ? "ON" : "OFF");
        }
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "BLE: MTU=%d", event->mtu.value);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        ESP_LOGI(TAG, "BLE: enc_change status=%d", event->enc_change.status);
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "BLE: encrypted=%d authenticated=%d bonded=%d",
                     desc.sec_state.encrypted,
                     desc.sec_state.authenticated,
                     desc.sec_state.bonded);
        }
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
            ESP_LOGI(TAG, "BLE: deleted stale bond, retrying pairing");
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGW(TAG, "BLE: unexpected passkey action=%d", event->passkey.params.action);
        return BLE_HS_ENOTSUP;
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

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE: host reset, reason=%d", reason);
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

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ESP_LOGI(TAG, "BLE: GAP+GATT+CAN service init OK");

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ESP_LOGI(TAG, "BLE: starting host task...");
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE: host task started");
}

static void led_task(void *arg)
{
    while (1) {
        if (pending_led_blink > 0) {
            uint8_t count = pending_led_blink;
            pending_led_blink = 0;
            for (int i = 0; i < count; i++) {
                gpio_set_level(LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(30));
                gpio_set_level(LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void ble_reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "BLE: re-advertising after disconnect...");
    ble_advertise();
    vTaskDelete(NULL);
}

// Metrics + vehicle data notification task (10 Hz)
static void metrics_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "Metrics task started (10 Hz)");

    while (1) {
        metrics_update();

        if (ble_conn_handle != BLE_HS_CONN_HANDLE_NONE && ble_data_notify) {
            // Vehicle data notification (16 bytes)
            uint8_t vbuf[16] = {0};
            uint16_t flags = 0;
            float v;
            v = vd_rpm(&g_vehicle);
            if (v >= 0) { flags |= (1 << 0); uint16_t r = (uint16_t)v; memcpy(&vbuf[2], &r, 2); }
            v = vd_speed(&g_vehicle);
            if (v >= 0) { flags |= (1 << 1); vbuf[4] = (uint8_t)v; }
            v = vd_coolant(&g_vehicle);
            if (v > -100) { flags |= (1 << 2); vbuf[5] = (uint8_t)(v + 40); }
            v = vd_throttle(&g_vehicle);
            if (v >= 0) { flags |= (1 << 3); vbuf[6] = (uint8_t)(v * 2.55f); }
            if (g_metrics.valid) {
                flags |= (1 << 7);
                uint16_t fr = (uint16_t)(g_metrics.fuel_rate_lph * 100);
                memcpy(&vbuf[11], &fr, 2);
                vbuf[15] = g_metrics.riding_score;
            }
            memcpy(vbuf, &flags, 2);

            struct os_mbuf *om = ble_hs_mbuf_from_flat(vbuf, 16);
            if (om) ble_gatts_notify_custom(ble_conn_handle, vehicle_data_chr_handle, om);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── PROD Mode (BLE + CAN) ──

static void start_prod_mode(void)
{
    ESP_LOGI(TAG, "=== PROD MODE (BLE + CAN + OBD2 + Metrics + ESP-NOW) ===");

    obd2_init();
    metrics_init();

    espnow_tx_init();

    ESP_LOGI(TAG, "Free heap before BLE: %lu", (unsigned long)esp_get_free_heap_size());
    ble_init();
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Free heap after BLE: %lu", (unsigned long)esp_get_free_heap_size());

    xTaskCreatePinnedToCore(can_sniffer_task, "can_sniffer", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(led_task, "led_task", 2048, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(metrics_task, "metrics", 3072, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(obd2_poll_task, "obd2_poll", 4096, NULL, 4, &obd2_poll_task_handle, 1);
    obd2_polling_active = true;
    xTaskCreate(espnow_tx_task, "espnow_tx", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "UDS polling + ESP-NOW sender started (PROD mode)");

    gpio_set_level(LED_GPIO, 1);
    ESP_LOGI(TAG, "LED off (GPIO%d)", LED_GPIO);
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

    gpio_reset_pin(LED_GPIO);
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&led_cfg);
    gpio_set_level(LED_GPIO, 1);

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
