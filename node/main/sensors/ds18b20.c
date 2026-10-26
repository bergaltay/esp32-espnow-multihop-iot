#include "ds18b20.h"
#include "config/node_config.h"
#include "config/pin_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if USE_REAL_SENSORS
#include "driver/gpio.h"
#include "rom/ets_sys.h"

static bool s_ready = false;

static inline void ow_drive_low(void) { gpio_set_level(PIN_DS18B20, 0); }
static inline void ow_release(void)   { gpio_set_level(PIN_DS18B20, 1); }
static inline int  ow_sample(void)    { return gpio_get_level(PIN_DS18B20); }

static esp_err_t ow_reset(void)
{
    ow_drive_low();
    ets_delay_us(480);
    ow_release();
    portDISABLE_INTERRUPTS();
    ets_delay_us(70);
    int present = (ow_sample() == 0);
    portENABLE_INTERRUPTS();
    ets_delay_us(410);
    return present ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void ow_write_bit(uint8_t bit)
{
    portDISABLE_INTERRUPTS();
    ow_drive_low();
    if (bit) {
        ets_delay_us(5);
        ow_release();
        portENABLE_INTERRUPTS();
        ets_delay_us(55);
    } else {
        ets_delay_us(60);
        ow_release();
        portENABLE_INTERRUPTS();
        ets_delay_us(10);
    }
}

static uint8_t ow_read_bit(void)
{
    portDISABLE_INTERRUPTS();
    ow_drive_low();
    ets_delay_us(3);
    ow_release();
    ets_delay_us(10);
    uint8_t bit = ow_sample();
    portENABLE_INTERRUPTS();
    ets_delay_us(47);
    return bit;
}

static void ow_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++, byte >>= 1) {
        ow_write_bit(byte & 0x01);
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (ow_read_bit() << i);
    }
    return byte;
}
#endif

static const char *TAG = "DS18B20";

esp_err_t ds18b20_init(void)
{
#if !USE_REAL_SENSORS
    return ESP_OK;
#else
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_DS18B20),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ow_release();

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    for (int attempt = 1; attempt <= 5; attempt++) {
        ret = ow_reset();
        if (ret == ESP_OK) break;
        ESP_LOGW(TAG, "Reset deneme %d/5 basarisiz", attempt);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Sensor algilanamadi (presence pulse yok, GP%d)", PIN_DS18B20);
        return ESP_ERR_NOT_FOUND;
    }

    s_ready = true;
    ESP_LOGI(TAG, "Sensor hazir (GP%d)", PIN_DS18B20);
    return ESP_OK;
#endif
}

esp_err_t ds18b20_read(int16_t *soil_temp_c10)
{
#if !USE_REAL_SENSORS
    *soil_temp_c10 = MOCK_SOIL_TEMP_C10;
    return ESP_OK;
#else
    if (!s_ready) {
        *soil_temp_c10 = 0x7FFF;
        return ESP_ERR_NOT_FOUND;
    }

    if (ow_reset() != ESP_OK) return ESP_ERR_NOT_FOUND;
    ow_write_byte(0xCC);  // Skip ROM
    ow_write_byte(0x44);  // Convert T
    vTaskDelay(pdMS_TO_TICKS(750));

    if (ow_reset() != ESP_OK) return ESP_ERR_NOT_FOUND;
    ow_write_byte(0xCC);  // Skip ROM
    ow_write_byte(0xBE);  // Read Scratchpad

    uint8_t scrpad[9];
    for (int i = 0; i < 9; i++) {
        scrpad[i] = ow_read_byte();
    }

    int16_t raw = (int16_t)((scrpad[1] << 8) | scrpad[0]);
    *soil_temp_c10 = (int16_t)(raw * 10 / 16);
    return ESP_OK;
#endif
}
