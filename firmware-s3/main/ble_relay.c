#include "ble_relay.h"
#include "espnow_rx.h"
#include "ble_ota.h"
#include "smart_features.h"
#include "flash_logger.h"
#include "wifi_portal.h"
#include "driver/gpio.h"
#include <string.h>
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_store.h"

static const char *TAG = "BLE";

#define BLE_DEVICE_NAME "ADV350-R"

static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool data_notify = false;

// ── UUIDs (identical to original firmware — Flutter app compatible) ──

static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t can_frame_chr_uuid =
    BLE_UUID128_INIT(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t ctrl_chr_uuid =
    BLE_UUID128_INIT(0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t vd_chr_uuid =
    BLE_UUID128_INIT(0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t mt_chr_uuid =
    BLE_UUID128_INIT(0xf4, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t dtc_chr_uuid =
    BLE_UUID128_INIT(0xf5, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t fw_chr_uuid =
    BLE_UUID128_INIT(0xf7, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t ota_chr_uuid =
    BLE_UUID128_INIT(0xf8, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t mgmt_chr_uuid =
    BLE_UUID128_INIT(0xf9, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static uint16_t vd_chr_handle;
static uint16_t mt_chr_handle;

// ── Build BLE packets from relay data ──

#define VD_BUF_LEN 23

static void build_vehicle_buf(uint8_t buf[VD_BUF_LEN])
{
    const relay_packet_t *p = &g_relay.pkt;
    memset(buf, 0, VD_BUF_LEN);
    if (!relay_data_fresh()) return;

    memcpy(&buf[0], &p->vd_flags, 2);
    memcpy(&buf[2], &p->vd_rpm, 2);
    buf[4] = p->vd_speed;
    buf[5] = p->vd_coolant_enc;
    buf[6] = p->vd_throttle_enc;
    buf[7] = p->vd_map;
    buf[8] = p->vd_iat_enc;
    memcpy(&buf[9], &p->vd_battery_cv, 2);
    memcpy(&buf[11], &p->vd_fuel_rate_x100, 2);
    memcpy(&buf[13], &p->vd_cvt_x100, 2);
    buf[15] = p->vd_score;

    float bt = smart_get_board_temp();
    int enc = (int)(bt + 40.0f);
    buf[16] = (uint8_t)(enc < 0 ? 0 : enc > 255 ? 255 : enc);

    buf[17] = p->vd_engine_load_enc;
    buf[18] = (uint8_t)p->vd_ign_timing;
    buf[19] = (uint8_t)p->vd_stft;
    memcpy(&buf[20], &p->vd_lambda_x1000, 2);
    buf[22] = p->vd_is_braking;
}

static void build_metrics_buf(int16_t buf[8])
{
    const relay_packet_t *p = &g_relay.pkt;
    memset(buf, 0, 16);
    if (!relay_data_fresh() || !p->metrics_valid) return;

    buf[0] = p->mt_accel_x100;
    buf[1] = p->mt_gforce_x1000;
    buf[2] = p->mt_fuel_kmpl_x10;
    buf[3] = p->mt_trip_fuel_x1000;
    buf[4] = p->mt_trip_dist_x100;
    buf[5] = p->mt_trip_avg_x10;
    buf[6] = p->mt_power_x100;
    buf[7] = p->mt_brake_dist_x10;
}

// ── GATT Callbacks ──

static int vd_access(uint16_t conn, uint16_t attr,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return 0;
    uint8_t buf[VD_BUF_LEN];
    build_vehicle_buf(buf);
    os_mbuf_append(ctxt->om, buf, VD_BUF_LEN);
    return 0;
}

static int mt_access(uint16_t conn, uint16_t attr,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return 0;
    int16_t buf[8];
    build_metrics_buf(buf);
    os_mbuf_append(ctxt->om, buf, sizeof(buf));
    return 0;
}

static int can_frame_access(uint16_t conn, uint16_t attr,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t zero = 0;
        os_mbuf_append(ctxt->om, &zero, 1);
    }
    return 0;
}

static int ctrl_access(uint16_t conn, uint16_t attr,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t status[3] = {
            data_notify ? 1 : 0,
            relay_data_fresh() ? 1 : 0,
            g_relay.peer_known ? 1 : 0,
        };
        os_mbuf_append(ctxt->om, status, sizeof(status));
        return 0;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    uint8_t val;
    if (OS_MBUF_PKTLEN(ctxt->om) < 1) return 0;
    os_mbuf_copydata(ctxt->om, 0, 1, &val);

    switch (val) {
    case 0x00:
        data_notify = false;
        ESP_LOGI(TAG, "Notify OFF");
        break;
    case 0x01:
        data_notify = true;
        ESP_LOGI(TAG, "Notify ON");
        break;
    case 0x02: {
        ESP_LOGI(TAG, "Ping — blink LED");
        for (int i = 0; i < 3; i++) {
            gpio_set_level(GPIO_NUM_2, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(GPIO_NUM_2, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        break;
    }
    case 0x10: case 0x11: case 0x12: case 0x13:
        ESP_LOGI(TAG, "Forward cmd 0x%02X to CAN board", val);
        relay_send_cmd(val);
        break;
    }
    return 0;
}

static int dtc_access(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return 0;
    uint8_t none = 0;
    os_mbuf_append(ctxt->om, &none, 1);
    return 0;
}

static int fw_access(uint16_t conn, uint16_t attr,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        os_mbuf_append(ctxt->om, FW_VERSION_S3, strlen(FW_VERSION_S3));
    }
    return 0;
}

// ── Management characteristic (0xf9) ──
// Read: 16-byte system info. Write: 0x01=clear logs, 0x02=restart

static int mgmt_access(uint16_t conn, uint16_t attr,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000);
        uint16_t heap_kb = (uint16_t)(esp_get_free_heap_size() / 1024);
        float bt = smart_get_board_temp();
        logger_stats_t ls = flash_logger_stats();
        uint16_t log_recs = (uint16_t)ls.record_count;
        uint16_t log_used = (uint16_t)(ls.used_bytes / 1024);
        uint16_t log_free = (uint16_t)(ls.free_bytes / 1024);

        uint8_t buf[16];
        buf[0] = (uint8_t)(uptime / 3600);
        buf[1] = (uint8_t)((uptime % 3600) / 60);
        memcpy(&buf[2], &heap_kb, 2);
        buf[4] = (uint8_t)(bt + 40.0f);
        memcpy(&buf[5], &log_recs, 2);
        memcpy(&buf[7], &log_used, 2);
        memcpy(&buf[9], &log_free, 2);
        buf[11] = (uint8_t)g_trip.trip_count;
        buf[12] = g_trip.state == TRIP_ACTIVE ? 1 : 0;
        buf[13] = g_relay.peer_known ? 1 : 0;
        buf[14] = (uint8_t)ble_ota_get_state();
        buf[15] = 0;
        os_mbuf_append(ctxt->om, buf, sizeof(buf));
        return 0;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    uint8_t cmd;
    if (OS_MBUF_PKTLEN(ctxt->om) < 1) return 0;
    os_mbuf_copydata(ctxt->om, 0, 1, &cmd);

    switch (cmd) {
    case 0x01:
        flash_logger_clear();
        ESP_LOGI(TAG, "BLE: logs cleared");
        break;
    case 0x02:
        ESP_LOGI(TAG, "BLE: restart requested");
        {
            const esp_timer_create_args_t a = {
                .callback = (esp_timer_cb_t)esp_restart, .name = "rst",
            };
            esp_timer_handle_t t;
            esp_timer_create(&a, &t);
            esp_timer_start_once(t, 500000);
        }
        break;
    }
    return 0;
}

// ── GATT Service Definition ──

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &can_frame_chr_uuid.u,
                .access_cb = can_frame_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &ctrl_chr_uuid.u,
                .access_cb = ctrl_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &vd_chr_uuid.u,
                .access_cb = vd_access,
                .val_handle = &vd_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &mt_chr_uuid.u,
                .access_cb = mt_access,
                .val_handle = &mt_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &dtc_chr_uuid.u,
                .access_cb = dtc_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &fw_chr_uuid.u,
                .access_cb = fw_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &ota_chr_uuid.u,
                .access_cb = ble_ota_access,
                .val_handle = &ble_ota_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &mgmt_chr_uuid.u,
                .access_cb = mgmt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            { 0 },
        },
    },
    { 0 },
};

// ── GAP ──

static void ble_advertise(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ble_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected — stopping WiFi AP");
            wifi_portal_stop();
        } else {
            ble_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        data_notify = false;
        ESP_LOGI(TAG, "Disconnected (reason=%d) — starting WiFi AP", event->disconnect.reason);
        wifi_portal_init();
        ble_advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == vd_chr_handle ||
            event->subscribe.attr_handle == mt_chr_handle) {
            data_notify = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "Data notify %s", data_notify ? "ON" : "OFF");
        }
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU=%d", event->mtu.value);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    }
    return 0;
}

static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)BLE_DEVICE_NAME;
    fields.name_len = strlen(BLE_DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &params, gap_event, NULL);
}

static void on_sync(void)
{
    ble_advertise();
    ESP_LOGI(TAG, "Advertising as '%s'", BLE_DEVICE_NAME);
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "Host reset, reason=%d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ── Notification Task (10 Hz) ──

static void notify_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Notify task started (10 Hz)");

    while (1) {
        uint16_t handle = ble_conn_handle;
        if (handle != BLE_HS_CONN_HANDLE_NONE && data_notify &&
            relay_data_fresh()) {
            uint8_t vbuf[VD_BUF_LEN];
            build_vehicle_buf(vbuf);
            struct os_mbuf *om = ble_hs_mbuf_from_flat(vbuf, VD_BUF_LEN);
            if (om) {
                int rc = ble_gatts_notify_custom(handle, vd_chr_handle, om);
                if (rc != 0) {
                    vTaskDelay(pdMS_TO_TICKS(200));
                    continue;
                }
            }

            // Metrics
            if (g_relay.pkt.metrics_valid) {
                int16_t mbuf[8];
                build_metrics_buf(mbuf);
                om = ble_hs_mbuf_from_flat(mbuf, sizeof(mbuf));
                if (om) ble_gatts_notify_custom(handle, mt_chr_handle, om);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── Init ──

void ble_relay_init(void)
{
    ESP_ERROR_CHECK(nimble_port_init());

    ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    nimble_port_freertos_init(host_task);
    xTaskCreatePinnedToCore(notify_task, "ble_notify", 3072, NULL, 4, NULL, 1);

    ESP_LOGI(TAG, "BLE relay initialized");
}
