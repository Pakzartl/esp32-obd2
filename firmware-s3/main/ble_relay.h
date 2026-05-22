#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifndef FW_VERSION_S3
#define FW_VERSION_S3  "0.0.0-dev"
#endif

void ble_relay_init(void);
bool ble_is_connected(void);
