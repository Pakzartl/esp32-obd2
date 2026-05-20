#pragma once
#include "esp_err.h"

esp_err_t espnow_tx_init(void);
void espnow_tx_task(void *arg);
