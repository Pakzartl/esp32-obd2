#include "status_led.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdbool.h>

#define LED_GPIO   48
#define BRIGHTNESS 16

static led_strip_handle_t s_strip = NULL;
static volatile bool s_blinking = false;
static volatile led_color_t s_base_color = LED_OFF;

esp_err_t status_led_init(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) return err;
    led_strip_clear(s_strip);
    return ESP_OK;
}

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void apply_color(led_color_t color)
{
    switch (color) {
    case LED_OFF:    set_rgb(0, 0, 0); break;
    case LED_RED:    set_rgb(BRIGHTNESS, 0, 0); break;
    case LED_GREEN:  set_rgb(0, BRIGHTNESS, 0); break;
    case LED_ORANGE: set_rgb(BRIGHTNESS, BRIGHTNESS / 2, 0); break;
    }
}

void status_led_set(led_color_t color)
{
    s_base_color = color;
    if (s_blinking) return;
    apply_color(color);
}

void status_led_blink(led_color_t color, int count, int interval_ms)
{
    if (count <= 0 || interval_ms <= 0) return;

    s_blinking = true;
    for (int i = 0; i < count; i++) {
        apply_color(color);
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
        apply_color(LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
    s_blinking = false;
    apply_color(s_base_color);
}
