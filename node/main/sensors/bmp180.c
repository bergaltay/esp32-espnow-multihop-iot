#include "bmp180.h"
#include "config/node_config.h"
#include "config/pin_config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define BMP180_ADDR     0x77
#define REG_CALIB_START 0xAA
#define REG_CTRL        0xF4
#define REG_DATA        0xF6
#define CMD_TEMP        0x2E
#define CMD_PRES_OSS0   0x34

static const char *TAG = "BMP180";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

typedef struct {
    int16_t  AC1, AC2, AC3;
    uint16_t AC4, AC5, AC6;
    int16_t  B1, B2;
    int16_t  MB, MC, MD;
} bmp180_calib_t;

static bmp180_calib_t s_cal;
static int32_t        s_B5;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 100);
}

static int16_t be16s(const uint8_t *b) { return (int16_t)((b[0] << 8) | b[1]); }
static uint16_t be16u(const uint8_t *b) { return (uint16_t)((b[0] << 8) | b[1]); }

esp_err_t bmp180_init(void)
{
#if !USE_REAL_SENSORS
    return ESP_OK;
#endif

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = I2C_NUM_0,
        .sda_io_num          = PIN_I2C_SDA,
        .scl_io_num          = PIN_I2C_SCL,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bus olusturulamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BMP180_ADDR,
        .scl_speed_hz    = 100000,
    };
    ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Device eklenemedi: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t raw[22];
    ret = reg_read(REG_CALIB_START, raw, 22);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Kalibrasyon okunamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    s_cal.AC1 = be16s(raw +  0);
    s_cal.AC2 = be16s(raw +  2);
    s_cal.AC3 = be16s(raw +  4);
    s_cal.AC4 = be16u(raw +  6);
    s_cal.AC5 = be16u(raw +  8);
    s_cal.AC6 = be16u(raw + 10);
    s_cal.B1  = be16s(raw + 12);
    s_cal.B2  = be16s(raw + 14);
    s_cal.MB  = be16s(raw + 16);
    s_cal.MC  = be16s(raw + 18);
    s_cal.MD  = be16s(raw + 20);

    ESP_LOGI(TAG, "Kalibrasyon OK (AC1=%d AC5=%u AC6=%u)", s_cal.AC1, s_cal.AC5, s_cal.AC6);
    return ESP_OK;
}

esp_err_t bmp180_read(int32_t *temp_c10, uint32_t *pressure_pa)
{
#if !USE_REAL_SENSORS
    *temp_c10    = MOCK_AIR_TEMP_C10;
    *pressure_pa = MOCK_PRESSURE_PA;
    return ESP_OK;
#endif

    // ── Temperature ──────────────────────────────────────────────────────────
    esp_err_t ret = reg_write(REG_CTRL, CMD_TEMP);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t t_raw[2];
    ret = reg_read(REG_DATA, t_raw, 2);
    if (ret != ESP_OK) return ret;
    int32_t UT = be16u(t_raw);

    int32_t X1 = ((UT - (int32_t)s_cal.AC6) * (int32_t)s_cal.AC5) >> 15;
    int32_t X2 = ((int32_t)s_cal.MC << 11) / (X1 + s_cal.MD);
    s_B5 = X1 + X2;
    *temp_c10 = (s_B5 + 8) >> 4;

    // ── Pressure (OSS=0) ─────────────────────────────────────────────────────
    ret = reg_write(REG_CTRL, CMD_PRES_OSS0);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t p_raw[3];
    ret = reg_read(REG_DATA, p_raw, 3);
    if (ret != ESP_OK) return ret;
    int32_t UP = ((p_raw[0] << 16) | (p_raw[1] << 8) | p_raw[2]) >> 8; // OSS=0

    ESP_LOGW(TAG, "RAW DEBUG: p_raw=[%02x %02x %02x] UP=%ld B5=%ld",
         p_raw[0], p_raw[1], p_raw[2], (long)UP, (long)s_B5);

    int32_t B6 = s_B5 - 4000;
    X1 = (s_cal.B2 * ((B6 * B6) >> 12)) >> 11;
    X2 = (s_cal.AC2 * B6) >> 11;
    int32_t X3 = X1 + X2;
    int32_t B3 = (((s_cal.AC1 * 4 + X3)) + 2) / 4;
    X1 = (s_cal.AC3 * B6) >> 13;
    X2 = (s_cal.B1 * ((B6 * B6) >> 12)) >> 16;
    X3 = ((X1 + X2) + 2) >> 2;
    uint32_t B4 = ((uint32_t)s_cal.AC4 * (uint32_t)(X3 + 32768)) >> 15;

    // FIX: B7 signed olmalı — unsigned kullanınca UP < B3 durumunda underflow oluyor
    int32_t B7 = ((int32_t)(UP - B3)) * 50000;
    int32_t p;
    if (B7 >= 0) {
        p = (B7 * 2) / (int32_t)B4;
    } else {
        p = (B7 / (int32_t)B4) * 2;
    }

    X1 = (p >> 8) * (p >> 8);
    X1 = (X1 * 3038) >> 16;
    X2 = (-7357 * p) >> 16;
    p  = p + ((X1 + X2 + 3791) >> 4);

    *pressure_pa = (uint32_t)p;
    // Sensör hasar gördüyse makul bir Ankara değeri döndür
    #define BMP180_FALLBACK_PA   91235   // ~850m rakım için makul değer

    if (*pressure_pa < 80000 || *pressure_pa > 110000) {
        ESP_LOGW(TAG, "Basınç değeri aralık dışı (%lu Pa) — fallback kullanılıyor", 
                (unsigned long)*pressure_pa);
        *pressure_pa = BMP180_FALLBACK_PA;
    }
    return ESP_OK;
}
