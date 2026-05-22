#include "ble_ota.h"
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "BLE_OTA";

#define OTA_CMD_BEGIN  0x01
#define OTA_CMD_DATA   0x02
#define OTA_CMD_END    0x03
#define OTA_CMD_ABORT  0x04

#define OTA_EVT_PROGRESS 0x01
#define OTA_EVT_DONE     0x02
#define OTA_EVT_ERROR    0xFE

uint16_t ble_ota_chr_handle;

static ota_state_t s_state = OTA_IDLE;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_target = NULL;
static uint32_t s_total_size = 0;
static uint32_t s_received = 0;
static uint8_t  s_last_pct = 0;

ota_state_t ble_ota_get_state(void) { return s_state; }

static void send_evt(uint16_t conn, uint8_t type, uint8_t val)
{
    uint8_t buf[2] = {type, val};
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, 2);
    if (om) ble_gatts_notify_custom(conn, ble_ota_chr_handle, om);
}

static void restart_cb(void *arg)
{
    esp_restart();
}

static void ota_begin(uint16_t conn, const uint8_t *data, uint16_t len)
{
    if (len < 5) {
        send_evt(conn, OTA_EVT_ERROR, 1);
        return;
    }

    if (s_state == OTA_RECEIVING) {
        esp_ota_abort(s_ota_handle);
    }
    if (s_state != OTA_IDLE) {
        s_state = OTA_IDLE;
        ESP_LOGW(TAG, "Reset stale OTA state");
    }

    memcpy(&s_total_size, data + 1, 4);

    s_target = esp_ota_get_next_update_partition(NULL);
    if (!s_target) {
        ESP_LOGE(TAG, "No OTA partition available");
        send_evt(conn, OTA_EVT_ERROR, 2);
        return;
    }

    esp_err_t err = esp_ota_begin(s_target, s_total_size, &s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        send_evt(conn, OTA_EVT_ERROR, 3);
        return;
    }

    s_received = 0;
    s_last_pct = 0;
    s_state = OTA_RECEIVING;
    ESP_LOGI(TAG, "OTA begin: %lu bytes -> %s", (unsigned long)s_total_size, s_target->label);
    send_evt(conn, OTA_EVT_PROGRESS, 0);
}

static void ota_data(uint16_t conn, const uint8_t *data, uint16_t len)
{
    if (s_state != OTA_RECEIVING || len < 2) return;

    const uint8_t *chunk = data + 1;
    uint16_t chunk_len = len - 1;

    esp_err_t err = esp_ota_write(s_ota_handle, chunk, chunk_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write failed at offset %lu", (unsigned long)s_received);
        esp_ota_abort(s_ota_handle);
        s_state = OTA_ERROR;
        send_evt(conn, OTA_EVT_ERROR, 4);
        return;
    }

    s_received += chunk_len;
    uint8_t pct = (uint8_t)((uint64_t)s_received * 100 / s_total_size);

    if (pct >= s_last_pct + 5 || s_received >= s_total_size) {
        send_evt(conn, OTA_EVT_PROGRESS, pct);
        s_last_pct = pct;
        ESP_LOGI(TAG, "OTA %u%% (%lu/%lu)",
                 pct, (unsigned long)s_received, (unsigned long)s_total_size);
    }
}

static void ota_end(uint16_t conn)
{
    if (s_state != OTA_RECEIVING) return;
    s_state = OTA_VALIDATING;

    ESP_LOGI(TAG, "Validating (%lu/%lu bytes)",
             (unsigned long)s_received, (unsigned long)s_total_size);

    esp_err_t err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Validation failed: %s", esp_err_to_name(err));
        s_state = OTA_ERROR;
        send_evt(conn, OTA_EVT_ERROR, 5);
        return;
    }

    err = esp_ota_set_boot_partition(s_target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(err));
        s_state = OTA_ERROR;
        send_evt(conn, OTA_EVT_ERROR, 6);
        return;
    }

    s_state = OTA_COMPLETE;
    ESP_LOGI(TAG, "OTA complete — rebooting in 2s");
    send_evt(conn, OTA_EVT_DONE, 0);

    const esp_timer_create_args_t timer_args = {
        .callback = restart_cb,
        .name = "ota_rst",
    };
    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_once(timer, 2000000);
}

static void ota_abort(uint16_t conn)
{
    if (s_state == OTA_RECEIVING) {
        esp_ota_abort(s_ota_handle);
    }
    s_state = OTA_IDLE;
    s_received = 0;
    s_total_size = 0;
    ESP_LOGW(TAG, "OTA aborted");
}

int ble_ota_access(uint16_t conn, uint16_t attr,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t buf[5];
        buf[0] = (uint8_t)s_state;
        memcpy(&buf[1], &s_received, 4);
        os_mbuf_append(ctxt->om, buf, sizeof(buf));
        return 0;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < 1 || len > 512) return 0;

    uint8_t tmp[512];
    os_mbuf_copydata(ctxt->om, 0, len, tmp);

    switch (tmp[0]) {
    case OTA_CMD_BEGIN:  ota_begin(conn, tmp, len);  break;
    case OTA_CMD_DATA:   ota_data(conn, tmp, len);   break;
    case OTA_CMD_END:    ota_end(conn);               break;
    case OTA_CMD_ABORT:  ota_abort(conn);             break;
    }

    return 0;
}
