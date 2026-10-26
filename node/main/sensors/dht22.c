#include "dht22.h"
#include "node_config.h"
#include "pin_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

static const char *TAG = "DHT22";

// ─── Timing helpers (used in real mode only) ──────────────────────────────────
// Wait until pin reaches `level` or timeout_us elapses.
static esp_err_t wait_level(int pin, int level, uint32_t timeout_us)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_us;
    while (gpio_get_level(pin) != level) {
        if (esp_timer_get_time() > deadline) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

// Measure duration (µs) that pin stays at `level`, up to timeout_us.
static uint32_t measure_pulse(int pin, int level, uint32_t timeout_us)
{
    int64_t start    = esp_timer_get_time();
    int64_t deadline = start + (int64_t)timeout_us;
    while (gpio_get_level(pin) == level) {
        if (esp_timer_get_time() > deadline) return timeout_us;
    }
    return (uint32_t)(esp_timer_get_time() - start);
}

void dht22_init(void)
{
#if USE_REAL_SENSORS
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_DHT22),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_LOGI(TAG, "DHT22 GPIO init OK (GP%d)", PIN_DHT22);
#else
    ESP_LOGI(TAG, "Mock mode — DHT22 GPIO not initialised");
#endif
}

esp_err_t dht22_read(int16_t *temp_c10, uint8_t *humidity_pct)
{
#if USE_REAL_SENSORS == 0
    // ── Mock mode: return realistic values with per-boot variation ────────────
    extern uint32_t g_boot_count;
    // Temperature varies ±2.0°C around MOCK base
    int16_t t = (int16_t)(MOCK_AIR_TEMP_C10 + (int16_t)(g_boot_count % 20) - 10);
    // Humidity varies ±4% around MOCK base
    uint8_t h = (uint8_t)(MOCK_AIR_HUMIDITY + (g_boot_count % 8));
    if (h > 99) h = 99;
    *temp_c10    = t;
    *humidity_pct = h;
    ESP_LOGI(TAG, "[MOCK] temp=%.1f°C  humidity=%u%%", t / 10.0f, h);
    return ESP_OK;

#else
    // ── Real DHT22 bit-bang protocol ─────────────────────────────────────────
    uint8_t data[5] = {0};

    // 1. Start signal: pull LOW for 2 ms, then release
    gpio_set_direction(PIN_DHT22, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_DHT22, 0);
    ets_delay_us(2000);  // 2ms busy-wait; vTaskDelay(pdMS_TO_TICKS(2))=0 at 100Hz tick

    // Disable interrupts BEFORE releasing the line so we don't miss DHT22's
    // 20-40 µs response window between host release and sensor pulling LOW.
    portDISABLE_INTERRUPTS();

    gpio_set_level(PIN_DHT22, 1);
    gpio_set_direction(PIN_DHT22, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_DHT22, GPIO_PULLUP_ONLY);

    // 2. DHT22 response: ~80 µs LOW then ~80 µs HIGH
    if (wait_level(PIN_DHT22, 0, 200) != ESP_OK) goto timeout;  // wait for LOW
    if (wait_level(PIN_DHT22, 1, 200) != ESP_OK) goto timeout;  // wait for HIGH
    if (wait_level(PIN_DHT22, 0, 200) != ESP_OK) goto timeout;  // wait for LOW again

    // 3. Read 40 bits
    for (int i = 0; i < 40; i++) {
        // Each bit starts with ~50 µs LOW
        if (wait_level(PIN_DHT22, 1, 100) != ESP_OK) goto timeout;
        // Measure HIGH pulse: < 35 µs = bit 0,  > 35 µs = bit 1
        uint32_t high_us = measure_pulse(PIN_DHT22, 1, 150);
        data[i / 8] <<= 1;
        if (high_us > 35) {
            data[i / 8] |= 1;
        }
    }

    portENABLE_INTERRUPTS();

    // 4. Verify checksum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        ESP_LOGE(TAG, "Checksum mismatch: calc=0x%02X recv=0x%02X", checksum, data[4]);
        return ESP_ERR_INVALID_CRC;
    }

    // 5. Parse humidity (16-bit, in tenths of %)
    uint16_t rh_raw = ((uint16_t)data[0] << 8) | data[1];
    *humidity_pct = (uint8_t)(rh_raw / 10);

    // 6. Parse temperature (bit15 = sign, remaining = value in tenths of °C)
    uint16_t temp_raw = ((uint16_t)(data[2] & 0x7F) << 8) | data[3];
    *temp_c10 = (int16_t)temp_raw;
    if (data[2] & 0x80) *temp_c10 = -*temp_c10;  // negative temp

    ESP_LOGI(TAG, "temp=%.1f°C  humidity=%u%%", *temp_c10 / 10.0f, *humidity_pct);
    return ESP_OK;

timeout:
    portENABLE_INTERRUPTS();
    ESP_LOGE(TAG, "Timeout during bit-bang read");
    return ESP_ERR_TIMEOUT;
#endif
}
