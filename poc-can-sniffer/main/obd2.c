#include "obd2.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UDS";

vehicle_data_t g_vehicle = {0};

// ── UDS Request (29-bit extended CAN) ──

static esp_err_t uds_send(uint32_t can_id, const uint8_t *data, uint8_t len)
{
    twai_message_t msg = {
        .identifier = can_id,
        .extd = 1,
        .data_length_code = 8,
    };
    memset(msg.data, 0xCC, 8); // padding per ISO-TP
    msg.data[0] = len; // PCI single frame
    memcpy(&msg.data[1], data, len);

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "TX fail 0x%08lX: %s", (unsigned long)can_id, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t uds_request(uint8_t service, const uint8_t *sub, uint8_t sub_len)
{
    uint8_t payload[7];
    payload[0] = service;
    if (sub_len > 0 && sub_len <= 6) {
        memcpy(&payload[1], sub, sub_len);
    }
    return uds_send(HONDA_UDS_REQUEST_ID, payload, 1 + sub_len);
}

// ── UDS Commands ──

void obd2_start_session(void)
{
    ESP_LOGI(TAG, "Starting diagnostic session...");
    uint8_t sub[] = {UDS_SESSION_DEFAULT};
    uds_request(UDS_DIAG_SESSION, sub, 1);
}

void obd2_tester_present(void)
{
    uint8_t sub[] = {0x00}; // sub-function: no response
    uds_request(UDS_TESTER_PRESENT, sub, 1);
}

void obd2_read_did(uint16_t did)
{
    uint8_t sub[] = {(uint8_t)(did >> 8), (uint8_t)(did & 0xFF)};
    uds_request(UDS_READ_DATA_BY_ID, sub, 2);
}

void obd2_read_dtcs(void)
{
    ESP_LOGI(TAG, "Reading DTCs...");
    g_vehicle.dtcs_valid = false;
    g_vehicle.dtc_count = 0;
    uint8_t sub[] = {0x02, 0xFF, 0xFF}; // reportDTCByStatusMask, all DTCs
    uds_request(UDS_READ_DTC, sub, 3);
}

// ── UDS Response Parser ──

static void parse_read_data_response(const uint8_t *data, uint8_t len)
{
    // data[0] = 0x62 (positive response for ReadDataByIdentifier)
    // data[1..2] = DID
    // data[3..] = value
    if (len < 4) return;

    uint16_t did = ((uint16_t)data[1] << 8) | data[2];
    const uint8_t *val = &data[3];
    uint8_t vlen = len - 3;

    switch (did) {
    case DID_RPM:
        if (vlen >= 2) {
            float rpm = ((float)val[0] * 256.0f + val[1]) / 4.0f;
            sensor_set(&g_vehicle.rpm, rpm);
            ESP_LOGD(TAG, "RPM: %.0f", rpm);
        }
        break;

    case DID_SPEED:
        if (vlen >= 1) {
            sensor_set(&g_vehicle.speed_kmh, (float)val[0]);
            ESP_LOGD(TAG, "Speed: %d km/h", val[0]);
        }
        break;

    case DID_COOLANT_TEMP:
        if (vlen >= 1) {
            float temp = (float)val[0] - 40.0f;
            sensor_set(&g_vehicle.coolant_temp_c, temp);
            ESP_LOGD(TAG, "Coolant: %.0f C", temp);
        }
        break;

    case DID_THROTTLE:
        if (vlen >= 1) {
            float tps = val[0] * 100.0f / 255.0f;
            sensor_set(&g_vehicle.throttle_pct, tps);
            ESP_LOGD(TAG, "Throttle: %.1f%%", tps);
        }
        break;

    case DID_ENGINE_LOAD:
        if (vlen >= 1) {
            sensor_set(&g_vehicle.engine_load_pct, val[0] * 100.0f / 255.0f);
        }
        break;

    case DID_MAP_KPA:
        if (vlen >= 1) {
            sensor_set(&g_vehicle.map_kpa, (float)val[0]);
        }
        break;

    case DID_INTAKE_AIR_TEMP:
        if (vlen >= 1) {
            sensor_set(&g_vehicle.intake_air_temp_c, (float)val[0] - 40.0f);
        }
        break;

    case DID_BATTERY_VOLTAGE:
        if (vlen >= 2) {
            sensor_set(&g_vehicle.battery_v, ((float)val[0] * 256.0f + val[1]) / 1000.0f);
        }
        break;

    case DID_FUEL_RATE:
        if (vlen >= 2) {
            sensor_set(&g_vehicle.fuel_rate_lph, ((float)val[0] * 256.0f + val[1]) * 0.05f);
        }
        break;

    case DID_INJECTOR_PW:
        if (vlen >= 2) {
            sensor_set(&g_vehicle.injector_pw_us, (float)val[0] * 256.0f + val[1]);
        }
        break;

    case DID_LAMBDA:
        if (vlen >= 2) {
            float lambda = ((float)val[0] * 256.0f + val[1]) / 32768.0f;
            sensor_set(&g_vehicle.lambda, lambda);
        }
        break;

    default:
        ESP_LOGI(TAG, "Unknown DID 0x%04X, %d bytes:", did, vlen);
        for (int i = 0; i < vlen && i < 8; i++) {
            printf("%02X ", val[i]);
        }
        printf("\n");
        break;
    }
}

static void parse_dtc_response(const uint8_t *data, uint8_t len)
{
    // data[0] = 0x59 (positive response for ReadDTCInformation)
    // data[1] = sub-function echo
    // data[2] = statusAvailabilityMask
    // data[3..] = DTC records: [DTC_high][DTC_mid][DTC_low][status]
    if (len < 3) return;

    g_vehicle.dtc_count = 0;
    int pos = 3;
    while (pos + 3 < len && g_vehicle.dtc_count < 16) {
        uint32_t dtc_raw = ((uint32_t)data[pos] << 16) | ((uint32_t)data[pos+1] << 8) | data[pos+2];
        uint8_t status = data[pos+3];
        if (dtc_raw == 0) { pos += 4; continue; }

        static const char prefix[] = {'P', 'C', 'B', 'U'};
        uint8_t type = (data[pos] >> 6) & 0x03;
        snprintf(g_vehicle.dtcs[g_vehicle.dtc_count].text, 6, "%c%02X%02X",
                 prefix[type], data[pos] & 0x3F, data[pos+1]);
        g_vehicle.dtcs[g_vehicle.dtc_count].raw = (uint16_t)(dtc_raw >> 8);
        g_vehicle.dtc_count++;

        ESP_LOGI(TAG, "DTC: %s (status=0x%02X)",
                 g_vehicle.dtcs[g_vehicle.dtc_count-1].text, status);
        pos += 4;
    }
    g_vehicle.dtcs_valid = true;
    ESP_LOGI(TAG, "DTCs: %d codes found", g_vehicle.dtc_count);
}

static void parse_negative_response(const uint8_t *data, uint8_t len)
{
    if (len < 3) return;
    // data[0]=0x7F, data[1]=rejected service, data[2]=NRC
    static const char *nrc_names[] = {
        [0x10] = "generalReject",
        [0x11] = "serviceNotSupported",
        [0x12] = "subFunctionNotSupported",
        [0x13] = "incorrectMessageLength",
        [0x14] = "responseTooLong",
        [0x22] = "conditionsNotCorrect",
        [0x31] = "requestOutOfRange",
        [0x33] = "securityAccessDenied",
        [0x72] = "generalProgrammingFailure",
        [0x78] = "requestCorrectlyReceived_responsePending",
    };
    const char *name = (data[2] < 0x80 && nrc_names[data[2]]) ? nrc_names[data[2]] : "unknown";
    ESP_LOGW(TAG, "NRC: service=0x%02X error=0x%02X (%s)", data[1], data[2], name);
}

// ── Frame Router ──

void obd2_process_frame(const twai_message_t *msg)
{
    // Honda UDS response (29-bit extended)
    if (msg->extd && msg->identifier == HONDA_UDS_RESPONSE_ID) {
        uint8_t pci = msg->data[0];
        uint8_t sf_len = pci & 0x0F; // single frame length
        if ((pci & 0xF0) != 0x00) return; // only handle single frames for now
        if (sf_len < 1 || sf_len > 7) return;

        uint8_t sid = msg->data[1];

        if (sid == (UDS_READ_DATA_BY_ID + UDS_POSITIVE_OFFSET)) {
            parse_read_data_response(&msg->data[1], sf_len);
        } else if (sid == (UDS_READ_DTC + UDS_POSITIVE_OFFSET)) {
            parse_dtc_response(&msg->data[1], sf_len);
        } else if (sid == (UDS_DIAG_SESSION + UDS_POSITIVE_OFFSET)) {
            g_vehicle.session_active = true;
            ESP_LOGI(TAG, "Diagnostic session active");
        } else if (sid == 0x7F) {
            parse_negative_response(&msg->data[1], sf_len);
        } else if (sid == (UDS_TESTER_PRESENT + UDS_POSITIVE_OFFSET)) {
            // keep-alive ACK, ignore
        } else {
            ESP_LOGI(TAG, "UDS response SID=0x%02X len=%d", sid, sf_len);
        }
    }
}

// ── DID Probe — discover which DIDs the ECU supports ──

void obd2_probe_dids(void)
{
    ESP_LOGI(TAG, "Probing DIDs...");
    g_vehicle.supported_did_count = 0;

    static const uint16_t probe_dids[] = {
        DID_RPM, DID_SPEED, DID_COOLANT_TEMP, DID_THROTTLE,
        DID_ENGINE_LOAD, DID_MAP_KPA, DID_INTAKE_AIR_TEMP,
        DID_BATTERY_VOLTAGE, DID_FUEL_RATE,
        DID_INJECTOR_PW, DID_LAMBDA,
        // Honda-specific DIDs to try
        0x0001, 0x0002, 0x0003, 0x0010, 0x0011, 0x0012,
        0x0020, 0x0021, 0x0100, 0x0101,
        0xF190, // VIN
        0xF194, // System supplier ECU HW number
    };

    for (int i = 0; i < sizeof(probe_dids) / sizeof(probe_dids[0]); i++) {
        obd2_read_did(probe_dids[i]);
        vTaskDelay(pdMS_TO_TICKS(150));

        // Check if we got a response (sensor updated)
        twai_message_t rx;
        while (twai_receive(&rx, pdMS_TO_TICKS(50)) == ESP_OK) {
            obd2_process_frame(&rx);
        }
    }

    g_vehicle.dids_probed = true;
    ESP_LOGI(TAG, "DID probe complete");
}

// ── UDS Polling Task ──

static const uint16_t poll_dids[] = {
    DID_RPM, DID_SPEED, DID_THROTTLE, DID_COOLANT_TEMP,
    DID_ENGINE_LOAD, DID_MAP_KPA, DID_INTAKE_AIR_TEMP,
    DID_BATTERY_VOLTAGE, DID_FUEL_RATE,
};
#define POLL_DID_COUNT (sizeof(poll_dids) / sizeof(poll_dids[0]))

void obd2_poll_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Start diagnostic session
    obd2_start_session();
    vTaskDelay(pdMS_TO_TICKS(500));

    // Probe DIDs
    obd2_probe_dids();
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "UDS polling started (%d DIDs in rotation)", (int)POLL_DID_COUNT);

    uint8_t idx = 0;
    uint32_t tester_present_counter = 0;

    while (1) {
        if (scan_pause_others) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        obd2_read_did(poll_dids[idx]);
        idx = (idx + 1) % POLL_DID_COUNT;

        // Send tester present every ~3 seconds to keep session alive
        tester_present_counter++;
        if (tester_present_counter >= 30) {
            obd2_tester_present();
            tester_present_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void obd2_init(void)
{
    memset(&g_vehicle, 0, sizeof(g_vehicle));
    ESP_LOGI(TAG, "Honda UDS engine initialized (29-bit extended CAN)");
}
