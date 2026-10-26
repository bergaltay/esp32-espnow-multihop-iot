#pragma once
#include "esp_err.h"
#include <stdint.h>

// Initialise GPIO for DHT22 (no-op in mock mode)
void dht22_init(void);

// Read temperature and humidity from DHT22 on PIN_DHT22.
// temp_c10 : temperature in 0.1°C units (e.g. 225 = 22.5°C, -5 = -0.5°C)
// humidity  : relative humidity in whole percent (0–100)
// Returns ESP_OK on success, ESP_ERR_TIMEOUT or ESP_ERR_INVALID_CRC on failure.
// NOTE: MOSFET (GP5) must be HIGH and stabilised before calling.
// NOTE: Minimum 2 s between consecutive reads (DHT22 hardware constraint).
esp_err_t dht22_read(int16_t *temp_c10, uint8_t *humidity_pct);
