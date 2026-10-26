#include "battery.h"
#include "node_config.h"
#include "pin_config.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "BATTERY";

#if USE_REAL_SENSORS
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_valid  = false;
#endif

void battery_init(void)
{
#if USE_REAL_SENSORS
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,      // 0–3300 mV range
        .bitwidth = ADC_BITWIDTH_12,
    };
    // Battery divider is on ADC1_CH2 (GP2)
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_2, &chan_cfg));

    // Attempt hardware calibration (curve-fitting preferred, line fitting fallback)
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle) == ESP_OK) {
        s_cali_valid = true;
    }
    ESP_LOGI(TAG, "ADC init OK, calibration: %s", s_cali_valid ? "curve" : "none");
#else
    ESP_LOGI(TAG, "Mock mode — ADC not initialised");
#endif
}

uint16_t battery_read_mv(void)
{
#if USE_REAL_BATTERY == 0
    // Mock: stable at ~3750 mV with minor variation per boot
    extern uint32_t g_boot_count;
    uint16_t mock = (uint16_t)(MOCK_BATTERY_MV - (g_boot_count % 50));
    ESP_LOGI(TAG, "[MOCK] battery_mv=%u", mock);
    return mock;
#else
    if (!s_adc_handle) {
        ESP_LOGE(TAG, "ADC not initialised");
        return 3700;
    }

    // Average 4 readings to reduce noise
    int32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        int raw;
        adc_oneshot_read(s_adc_handle, ADC_CHANNEL_2, &raw);
        sum += raw;
    }
    int raw_avg = (int)(sum / 4);

    int adc_mv = 0;
    if (s_cali_valid) {
        adc_cali_raw_to_voltage(s_cali_handle, raw_avg, &adc_mv);
    } else {
        // Fallback: linear approximation (±2% error without calibration)
        adc_mv = (int)(raw_avg * 3300 / 4095);
    }

    // Scale for voltage divider: 10K + 27K
    uint16_t battery_mv = (uint16_t)((uint32_t)adc_mv * (10 + 27) / 27);
    ESP_LOGI(TAG, "raw=%d  adc_mv=%d  battery_mv=%u", raw_avg, adc_mv, battery_mv);
    return battery_mv;
#endif
}

uint8_t battery_mv_to_percent(uint16_t mv)
{
    if (mv >= 4200) return 100;
    if (mv <= 3200) return 0;
    // Linear map: 3200=0%, 3700=50%, 4200=100%
    return (uint8_t)((mv - 3200) * 100 / (4200 - 3200));
}

adc_oneshot_unit_handle_t battery_get_adc_handle(void)
{
#if USE_REAL_SENSORS
    return s_adc_handle;
#else
    return NULL;
#endif
}
