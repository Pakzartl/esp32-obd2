#pragma once
#include <stdint.h>
#include "host/ble_hs.h"

typedef enum {
    OTA_IDLE,
    OTA_RECEIVING,
    OTA_VALIDATING,
    OTA_COMPLETE,
    OTA_ERROR,
} ota_state_t;

extern uint16_t ble_ota_chr_handle;

int         ble_ota_access(uint16_t conn, uint16_t attr,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);
ota_state_t ble_ota_get_state(void);
