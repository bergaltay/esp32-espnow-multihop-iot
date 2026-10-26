#include "sw520d.h"
#include "config/node_config.h"
#include "config/pin_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "SW520D";

esp_err_t sw520d_init(void)
{
#if !USE_REAL_SENSORS
    return ESP_OK;
#endif

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_TILT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GP%d hazir (pull-up)", PIN_TILT);
    }
    return ret;
}

esp_err_t sw520d_read(bool *tilted)
{
#if !USE_REAL_SENSORS
    *tilted = false;
    return ESP_OK;
#endif

    // Pull-up: HIGH = ball connects pin to VCC = tilted
    *tilted = (gpio_get_level(PIN_TILT) == 1);
    return ESP_OK;
}
