#pragma once
#include <stdint.h>
#include "esp_adc/adc_oneshot.h"

// Initialise ADC for soil moisture using a shared ADC1 handle from battery_init().
// ESP-IDF adc_oneshot does not allow two handles for the same unit, so battery's
// handle is reused. Pass NULL in mock mode (no-op).
void soil_moisture_init(adc_oneshot_unit_handle_t shared_adc);

// Raw ADC read from capacitive soil sensor on GP1 (ADC1_CH1).
// Returns raw ADC counts (0–4095). Call BEFORE esp_now_init().
uint16_t soil_moisture_read_raw(void);

// Apply temperature compensation:
//   compensated = raw_voltage - (air_temp_c - 25.0) * 0.3
// Returns compensated value ×100 for integer representation.
// air_temp_c is from DHT22 (already read before calling this).
uint16_t soil_moisture_compensate(uint16_t raw_adc, float air_temp_c);
