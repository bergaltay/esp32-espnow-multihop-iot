#pragma once
#include <stdint.h>
#include "esp_adc/adc_oneshot.h"

// Initialise ADC for battery reading (no-op in mock mode)
void battery_init(void);

// Read battery voltage in millivolts
// Voltage divider: 10K + 27K on GP2 (ADC1_CH2)
// Formula: battery_mv = adc_mv * (10+27) / 27
uint16_t battery_read_mv(void);

// Simple linear map: 4200mV=100%, 3700mV=50%, 3200mV=0%
uint8_t battery_mv_to_percent(uint16_t mv);

// Returns the ADC1 unit handle created by battery_init().
// Soil moisture sensor reuses this handle to avoid double-init of ADC_UNIT_1.
adc_oneshot_unit_handle_t battery_get_adc_handle(void);
