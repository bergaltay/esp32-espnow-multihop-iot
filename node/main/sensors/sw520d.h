#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t sw520d_init(void);
esp_err_t sw520d_read(bool *tilted);
