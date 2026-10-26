#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t bmp180_init(void);
esp_err_t bmp180_read(int32_t *temp_c10, uint32_t *pressure_pa);
