#include "soil_moisture.h"
#include "node_config.h"
#include "pin_config.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "SOIL";

#if USE_REAL_SENSORS
static adc_oneshot_unit_handle_t s_adc_handle  = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_valid  = false;
#endif

void soil_moisture_init(adc_oneshot_unit_handle_t shared_adc)
{
#if USE_REAL_SENSORS
    // Reuse battery's ADC1 handle — ESP-IDF forbids two handles for the same unit.
    s_adc_handle = shared_adc;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_1, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    s_cali_valid = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle) == ESP_OK);
    ESP_LOGI(TAG, "Soil ADC init OK");
#else
    (void)shared_adc;
    ESP_LOGI(TAG, "Mock mode — soil ADC not initialised");
#endif
}

uint16_t soil_moisture_read_raw(void)
{
#if USE_REAL_SENSORS == 0
    extern uint32_t g_boot_count;
    // Simulate soil drying slowly over time (mock)
    uint16_t mock = (uint16_t)(MOCK_SOIL_MOISTURE_RAW - (g_boot_count % 300));
    ESP_LOGI(TAG, "[MOCK] soil_raw=%u", mock);
    return mock;
#else
    if (!s_adc_handle) {
        ESP_LOGE(TAG, "ADC not initialised");
        return 2048;
    }
    // Average 4 readings
    int32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        int raw;
        adc_oneshot_read(s_adc_handle, ADC_CHANNEL_1, &raw);
        sum += raw;
    }
    uint16_t raw_avg = (uint16_t)(sum / 4);
    ESP_LOGI(TAG, "soil_raw=%u", raw_avg);
    return raw_avg;
#endif
}

uint16_t soil_moisture_compensate(uint16_t raw_adc, float air_temp_c)
{
    float raw_mv = raw_adc * 3300.0f / 4095.0f;

    // Temperature compensation
    float compensated = raw_mv - (air_temp_c - TEMP_COMPENSATION_REF) * TEMP_COMPENSATION_COEFF;
    if (compensated < 0.0f) compensated = 0.0f;

    // Capacitive sensor: dry ~2800mV, wet ~1200mV
    #define SOIL_DRY_MV   2800.0f
    #define SOIL_WET_MV   1200.0f
    float pct = (SOIL_DRY_MV - compensated) / (SOIL_DRY_MV - SOIL_WET_MV) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    // ×100 for precision — 5623 = 56.23%
    uint16_t result = (uint16_t)(pct * 100.0f);
    ESP_LOGI(TAG, "raw_mv=%.1f  pct=%.2f%%  compensated_×100=%u",
             raw_mv, pct, result);
    return result;
}
