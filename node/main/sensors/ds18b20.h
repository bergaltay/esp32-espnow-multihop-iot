#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t ds18b20_init(void);
esp_err_t ds18b20_read(int16_t *soil_temp_c10);
