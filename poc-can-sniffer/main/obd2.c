#include "obd2.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OBD2";

vehicle_data_t g_vehicle = {0};

// ── OBD2 Request/Response ──

static esp_err_t obd2_send_request(uint8_t mode, uint8_t pid)
{
    twai_message_t msg = {
        .identifier = OBD2_REQUEST_ID,
        .data_length_code = 8,
        .data = {0x02, mode, pid, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "TX fail PID 0x%02X: %s", pid, esp_err_to_name(err));
    }
    return err;
}

void obd2_request_pid(uint8_t pid)
{
    obd2_send_request(0x01, pid);
}

bool obd2_is_pid_supported(uint8_t pid)
{
    if (!g_vehicle.pids_probed) return false;
    uint8_t group = pid / 32;
    uint8_t bit = 31 - (pid % 32);
    if (group >= 4) return false;
    return (g_vehicle.supported_pids[group] >> bit) & 1;
}

void obd2_probe_supported_pids(void)
{
    ESP_LOGI(TAG, "Probing supported PIDs...");
    obd2_send_request(0x01, PID_SUPPORTED_01_20);
    vTaskDelay(pdMS_TO_TICKS(200));
    obd2_send_request(0x01, PID_SUPPORTED_21_40);
    vTaskDelay(pdMS_TO_TICKS(200));
    obd2_send_request(0x01, PID_SUPPORTED_41_60);
    vTaskDelay(pdMS_TO_TICKS(200));
    obd2_send_request(0x01, PID_SUPPORTED_61_80);
}

// ── OBD2 Response Parser ──

static void parse_obd2_response(const uint8_t *data, uint8_t dlc)
{
    if (dlc < 3 || data[0] < 3) return;
    uint8_t mode = data[1];
    uint8_t pid = data[2];

    if (mode != 0x41) return; // Mode 01 response

    switch (pid) {
    case PID_SUPPORTED_01_20:
        if (data[0] >= 6) {
            g_vehicle.supported_pids[0] = ((uint32_t)data[3] << 24) |
                ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 8) | data[6];
            g_vehicle.pids_probed = true;
            ESP_LOGI(TAG, "PIDs 01-20: 0x%08lX", (unsigned long)g_vehicle.supported_pids[0]);
        }
        break;
    case PID_SUPPORTED_21_40:
        if (data[0] >= 6) {
            g_vehicle.supported_pids[1] = ((uint32_t)data[3] << 24) |
                ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 8) | data[6];
            ESP_LOGI(TAG, "PIDs 21-40: 0x%08lX", (unsigned long)g_vehicle.supported_pids[1]);
        }
        break;
    case PID_SUPPORTED_41_60:
        if (data[0] >= 6) {
            g_vehicle.supported_pids[2] = ((uint32_t)data[3] << 24) |
                ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 8) | data[6];
        }
        break;
    case PID_SUPPORTED_61_80:
        if (data[0] >= 6) {
            g_vehicle.supported_pids[3] = ((uint32_t)data[3] << 24) |
                ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 8) | data[6];
        }
        break;
    case PID_RPM:
        if (data[0] >= 4)
            sensor_set(&g_vehicle.rpm, ((float)data[3] * 256.0f + data[4]) / 4.0f);
        break;
    case PID_SPEED:
        if (data[0] >= 3)
            sensor_set(&g_vehicle.speed_kmh, (float)data[3]);
        break;
    case PID_COOLANT_TEMP:
        if (data[0] >= 3)
            sensor_set(&g_vehicle.coolant_temp_c, (float)data[3] - 40.0f);
        break;
    case PID_THROTTLE:
        if (data[0] >= 3)
            sensor_set(&g_vehicle.throttle_pct, data[3] * 100.0f / 255.0f);
        break;
    case PID_ENGINE_LOAD:
        if (data[0] >= 3)
            sensor_set(&g_vehicle.engine_load_pct, data[3] * 100.0f / 255.0f);
        break;
    case PID_MAP_KPA:
        if (data[0] >= 3)
            sensor_set(&g_vehicle.map_kpa, (float)data[3]);
        break;
    case PID_INTAKE_AIR_TEMP:
        if (data[0] >= 3)
            sensor_set(&g_vehicle.intake_air_temp_c, (float)data[3] - 40.0f);
        break;
    case PID_BATTERY_VOLTAGE:
        if (data[0] >= 4)
            sensor_set(&g_vehicle.battery_v, ((float)data[3] * 256.0f + data[4]) / 1000.0f);
        break;
    case PID_FUEL_RATE:
        if (data[0] >= 4)
            sensor_set(&g_vehicle.fuel_rate_lph, ((float)data[3] * 256.0f + data[4]) * 0.05f);
        break;
    case PID_FUEL_LEVEL:
        if (data[0] >= 3)
            sensor_set(&g_vehicle.fuel_level_pct, data[3] * 100.0f / 255.0f);
        break;
    }
}

// ── DTC Reader ──

static void dtc_raw_to_text(uint16_t raw, char *out)
{
    static const char prefix[] = {'P', 'C', 'B', 'U'};
    uint8_t type = (raw >> 14) & 0x03;
    uint8_t d2 = (raw >> 12) & 0x03;
    uint8_t d3 = (raw >> 8) & 0x0F;
    uint8_t d4 = (raw >> 4) & 0x0F;
    uint8_t d5 = raw & 0x0F;
    snprintf(out, 6, "%c%d%X%X%X", prefix[type], d2, d3, d4, d5);
}

static void parse_dtc_response(const uint8_t *data, uint8_t dlc)
{
    if (dlc < 2 || data[1] != 0x43) return; // Mode 03 response
    uint8_t count = data[2];
    if (count > 16) count = 16;

    g_vehicle.dtc_count = 0;
    for (int i = 0; i < count && (3 + i * 2 + 1) < dlc; i++) {
        uint16_t raw = ((uint16_t)data[3 + i * 2] << 8) | data[4 + i * 2];
        if (raw == 0) continue;
        g_vehicle.dtcs[g_vehicle.dtc_count].raw = raw;
        dtc_raw_to_text(raw, g_vehicle.dtcs[g_vehicle.dtc_count].text);
        g_vehicle.dtc_count++;
    }
    g_vehicle.dtcs_valid = true;
    ESP_LOGI(TAG, "DTCs: %d codes found", g_vehicle.dtc_count);
    for (int i = 0; i < g_vehicle.dtc_count; i++) {
        ESP_LOGI(TAG, "  DTC[%d]: %s (0x%04X)", i, g_vehicle.dtcs[i].text, g_vehicle.dtcs[i].raw);
    }
}

void obd2_read_dtcs(void)
{
    ESP_LOGI(TAG, "Reading DTCs...");
    g_vehicle.dtcs_valid = false;
    g_vehicle.dtc_count = 0;
    twai_message_t msg = {
        .identifier = OBD2_REQUEST_ID,
        .data_length_code = 8,
        .data = {0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    twai_transmit(&msg, pdMS_TO_TICKS(50));
}

// ── Honda Proprietary Broadcast Decoder ──
//
// CAN IDs below are PREDICTIONS based on opendbc Honda car data
// and community research. They MUST be verified with a real ADV350
// capture. Update these IDs after first vehicle test.
//
// The decoder silently ignores frames it doesn't recognize,
// so wrong IDs simply produce no data (no crash).

#define HONDA_ID_ENGINE_1    0x158   // predicted: RPM + speed
#define HONDA_ID_ENGINE_2    0x17C   // predicted: RPM alt + throttle
#define HONDA_ID_THROTTLE    0x130   // predicted: TPS raw
#define HONDA_ID_SPEED       0x1B0   // predicted: wheel speed
#define HONDA_ID_TEMPS       0x324   // predicted: coolant + IAT
#define HONDA_ID_ABS         0x1D0   // predicted: ABS wheel speeds
#define HONDA_ID_MISC        0x294   // predicted: battery + lambda

void honda_decode_frame(uint32_t can_id, const uint8_t *data, uint8_t dlc)
{
    switch (can_id) {

    case HONDA_ID_ENGINE_1:
        if (dlc >= 4) {
            float rpm = ((float)data[0] * 256.0f + data[1]) / 4.0f;
            if (rpm >= 0 && rpm < 15000)
                sensor_set(&g_vehicle.honda_rpm, rpm);
            float speed = ((float)data[2] * 256.0f + data[3]) * 0.01f;
            if (speed >= 0 && speed < 300)
                sensor_set(&g_vehicle.honda_speed, speed);
        }
        break;

    case HONDA_ID_ENGINE_2:
        if (dlc >= 4) {
            float rpm = ((float)data[0] * 256.0f + data[1]) / 4.0f;
            if (rpm >= 0 && rpm < 15000)
                sensor_set(&g_vehicle.honda_rpm, rpm);
            float tps = data[2] * 100.0f / 255.0f;
            sensor_set(&g_vehicle.honda_throttle, tps);
        }
        break;

    case HONDA_ID_THROTTLE:
        if (dlc >= 1) {
            float tps = data[0] * 100.0f / 255.0f;
            sensor_set(&g_vehicle.honda_throttle, tps);
        }
        break;

    case HONDA_ID_SPEED:
        if (dlc >= 4) {
            float front = ((float)data[0] * 256.0f + data[1]) * 0.01f;
            float rear  = ((float)data[2] * 256.0f + data[3]) * 0.01f;
            if (front >= 0 && front < 300)
                sensor_set(&g_vehicle.wheel_speed_front, front);
            if (rear >= 0 && rear < 300)
                sensor_set(&g_vehicle.wheel_speed_rear, rear);
            sensor_set(&g_vehicle.honda_speed, rear);
        }
        break;

    case HONDA_ID_TEMPS:
        if (dlc >= 2) {
            float coolant = (float)data[0] - 40.0f;
            if (coolant > -40 && coolant < 200)
                sensor_set(&g_vehicle.honda_coolant, coolant);
            float iat = (float)data[1] - 40.0f;
            if (iat > -40 && iat < 100)
                sensor_set(&g_vehicle.intake_air_temp_c, iat);
        }
        break;

    case HONDA_ID_ABS:
        if (dlc >= 4) {
            float front = ((float)data[0] * 256.0f + data[1]) * 0.01f;
            float rear  = ((float)data[2] * 256.0f + data[3]) * 0.01f;
            if (front >= 0 && front < 300)
                sensor_set(&g_vehicle.wheel_speed_front, front);
            if (rear >= 0 && rear < 300)
                sensor_set(&g_vehicle.wheel_speed_rear, rear);
        }
        break;

    case HONDA_ID_MISC:
        if (dlc >= 2) {
            float batt = data[0] * 0.1f;
            if (batt > 5 && batt < 20)
                sensor_set(&g_vehicle.battery_v, batt);
            if (dlc >= 3) {
                float lambda = data[1] / 128.0f;
                if (lambda > 0.5f && lambda < 2.0f)
                    sensor_set(&g_vehicle.honda_lambda, lambda);
            }
        }
        break;

    default:
        break;
    }
}

// ── Frame Router ──

void obd2_process_frame(const twai_message_t *msg)
{
    if (msg->identifier == OBD2_RESPONSE_ID && !msg->extd) {
        parse_obd2_response(msg->data, msg->data_length_code);
    } else if (msg->identifier == OBD2_RESPONSE_ID - 1 && msg->data[1] == 0x43) {
        parse_dtc_response(msg->data, msg->data_length_code);
    } else if (!msg->extd && msg->identifier < 0x800) {
        honda_decode_frame(msg->identifier, msg->data, msg->data_length_code);
    }
}

// ── OBD2 Polling Task ──

static const uint8_t poll_pids[] = {
    PID_RPM, PID_SPEED, PID_THROTTLE, PID_COOLANT_TEMP,
    PID_ENGINE_LOAD, PID_MAP_KPA, PID_INTAKE_AIR_TEMP,
    PID_BATTERY_VOLTAGE, PID_FUEL_RATE, PID_FUEL_LEVEL,
};
#define POLL_PID_COUNT (sizeof(poll_pids) / sizeof(poll_pids[0]))

void obd2_poll_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    obd2_probe_supported_pids();
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "OBD2 polling started (%d PIDs in rotation)", (int)POLL_PID_COUNT);

    uint8_t idx = 0;
    while (1) {
        uint8_t pid = poll_pids[idx];
        if (!g_vehicle.pids_probed || obd2_is_pid_supported(pid)) {
            obd2_request_pid(pid);
        }
        idx = (idx + 1) % POLL_PID_COUNT;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void obd2_init(void)
{
    memset(&g_vehicle, 0, sizeof(g_vehicle));
    ESP_LOGI(TAG, "OBD2 engine initialized");
}
