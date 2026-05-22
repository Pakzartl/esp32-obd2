#pragma once
#include "esp_err.h"

typedef enum {
    LED_OFF,
    LED_RED,
    LED_GREEN,
    LED_ORANGE,
} led_color_t;

esp_err_t status_led_init(void);
void status_led_set(led_color_t color);
void status_led_blink(led_color_t color, int count, int interval_ms);
