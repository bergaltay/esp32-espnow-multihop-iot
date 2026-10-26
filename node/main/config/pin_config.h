#pragma once

// ─── V1 Active Pins ───────────────────────────────────────────────────────────
#define PIN_SOIL_MOISTURE   1    // GP1 → ADC1_CH1 (capacitive soil sensor)
#define PIN_BATTERY_ADC     2    // GP2 → ADC1_CH2 (10K+27K voltage divider)
#define PIN_DHT22           3    // GP3 → DHT2 2 data (4.7K pull-up required)
#define PIN_MOSFET_GATE     5    // GP5 → MOSFET gate (HIGH = sensors powered)

// ─── V2 Active Pins ───────────────────────────────────────────────────────────
#define PIN_DS18B20     14   // GP14 → Dallas 1-Wire soil temp
#define PIN_TILT        6    // GP6  → SW-520D tilt switch
#define PIN_I2C_SDA     18   // GP18 → BMP180 SDA
#define PIN_I2C_SCL     19   // GP19 → BMP180 SCL

// ─── Pins to AVOID (hardware conflicts on ESP32-C6 Super Mini) ────────────────
// GP0  — boot-strapping pin, do not use as ADC
// GP8  — onboard WS2812 RGB LED, DO NOT USE
// GP9  — BOOT pin, leave unconnected
// GP15 — onboard LED, avoid
