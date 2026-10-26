#include "brownout.h"
#include "node_config.h"
#include "esp_log.h"

static const char *TAG = "BROWNOUT";

bool brownout_check(uint16_t battery_mv)
{
    if (battery_mv < BROWNOUT_THRESHOLD_MV) {
        ESP_LOGW(TAG, "⚠ BROWNOUT: %u mV < threshold %u mV — skipping sensors + radio",
                 battery_mv, BROWNOUT_THRESHOLD_MV);
        return true;
    }
    ESP_LOGD(TAG, "Battery OK: %u mV", battery_mv);
    return false;
}
