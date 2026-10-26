#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "config/pin_config.h"
#include "config/node_config.h"
#include "utils/rtc_store.h"
#include "sensors/battery.h"
#include "sensors/dht22.h"
#include "sensors/soil_moisture.h"
#include "sensors/bmp180.h"
#include "sensors/ds18b20.h"
#include "sensors/sw520d.h"
#include "comm/espnow_manager.h"
#include "comm/sync.h"
#include "comm/setup.h"
#include "network/neighbor_table.h"
#include "network/tpc.h"
#include "network/relay.h"
#include "power/sleep_manager.h"
#include "power/brownout.h"
#include "packet.h"

static const char *TAG = "MAIN";

static void gpio_init_all(void)
{
    gpio_config_t mosfet_cfg = {
        .pin_bit_mask = (1ULL << PIN_MOSFET_GATE),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&mosfet_cfg));
    gpio_set_level(PIN_MOSFET_GATE, 0);
}

void app_main(void)
{
    // Cycle start timestamp — used by sleep_manager to compute active phase duration.
    // Must be captured before any blocking work so elapsed time is accurate.
    int64_t cycle_start_us = esp_timer_get_time();

    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    gpio_init_all();
    rtc_store_on_boot();

    // ── Banner ────────────────────────────────────────────────────────────────
    const char *role_str = (NODE_ID == 2) ? "RELAY" : "LEAF ";
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Node %d (%s)  Boot #%-4lu  sensors=%-4s     ║",
             NODE_ID, role_str, (unsigned long)g_boot_count,
             USE_REAL_SENSORS ? "REAL" : "MOCK");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

    // ── Step 1: Battery (ADC must happen before ESP-NOW init) ─────────────────
    battery_init();
    uint16_t battery_mv  = battery_read_mv();
    uint8_t  battery_pct = battery_mv_to_percent(battery_mv);

    // ── Step 2: Brownout guard ────────────────────────────────────────────────
    if (brownout_check(battery_mv)) {
        ESP_LOGW(TAG, "Brownout (%u mV) — skipping comms, long sleep", battery_mv);
        uint64_t sleep_us = SLEEP_DURATION_NIGHT_US;
        rtc_store_update_time(sleep_us);
        esp_sleep_enable_timer_wakeup(sleep_us);
        esp_deep_sleep_start();
    }

    // ── Step 3: Read sensors (MOSFET power cycle, ADC before ESP-NOW) ─────────
#if USE_REAL_SENSORS
    gpio_set_level(PIN_MOSFET_GATE, 1);
    vTaskDelay(pdMS_TO_TICKS(MOSFET_STABILIZE_MS));
#endif

    soil_moisture_init(battery_get_adc_handle());
    dht22_init();
    bmp180_init();
    ds18b20_init();
    sw520d_init();

    uint16_t soil_raw     = soil_moisture_read_raw();
    int16_t  air_temp_c10 = MOCK_AIR_TEMP_C10;
    uint8_t  air_humidity = MOCK_AIR_HUMIDITY;
    esp_err_t dht_ret = dht22_read(&air_temp_c10, &air_humidity);
    if (dht_ret != ESP_OK) {
        ESP_LOGW(TAG, "DHT22 okuma hatasi (%s) — varsayilan degerler kullaniliyor",
                 esp_err_to_name(dht_ret));
    }

    int32_t  bmp_temp_c10  = MOCK_AIR_TEMP_C10;
    uint32_t pressure_pa   = MOCK_PRESSURE_PA;
    int16_t  soil_temp_c10 = 0x7FFF;
    bool     tilted        = false;

    esp_err_t bmp_ret = bmp180_read(&bmp_temp_c10, &pressure_pa);
    if (bmp_ret != ESP_OK) {
        ESP_LOGW(TAG, "BMP180 okuma hatasi (%s)", esp_err_to_name(bmp_ret));
    }

    esp_err_t ds_ret = ds18b20_read(&soil_temp_c10);
    if (ds_ret != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 okuma hatasi (%s)", esp_err_to_name(ds_ret));
        soil_temp_c10 = 0x7FFF;
    }

    sw520d_read(&tilted);

#if USE_REAL_SENSORS
    gpio_set_level(PIN_MOSFET_GATE, 0);
#endif

    float    air_temp_c  = air_temp_c10 / 10.0f;
    uint16_t soil_compen = soil_moisture_compensate(soil_raw, air_temp_c);

    ESP_LOGI(TAG, "─── Sensor Okumalar ─────────────────────────────");
    ESP_LOGI(TAG, "  Batarya  : %u mV (%u%%)", battery_mv, battery_pct);
    ESP_LOGI(TAG, "  Toprak   : raw=%u  comp=%u (%.2f%%)",
             soil_raw, soil_compen, soil_compen / 100.0f);
    ESP_LOGI(TAG, "  Sicaklik : %.1f°C  Nem: %u%%", air_temp_c, air_humidity);
    ESP_LOGI(TAG, "  BMP180   : %.1f°C  %lu Pa", bmp_temp_c10 / 10.0f, (unsigned long)pressure_pa);
    ESP_LOGI(TAG, "  DS18B20  : %.1f°C (toprak)", soil_temp_c10 == 0x7FFF ? 0.0f : soil_temp_c10 / 10.0f);
    ESP_LOGI(TAG, "  SW-520D  : %s", tilted ? "EGIM VAR" : "normal");

    // ── Step 4: Build flags ───────────────────────────────────────────────────
    uint8_t flags = 0;
    if (air_temp_c10 < FROST_THRESHOLD_C10) {
        flags |= FLAG_FROST_ALERT;
        ESP_LOGW(TAG, "  ⚠ DON ALARMI — sicaklik=%.1f°C", air_temp_c);
    }
    if (tilted) {
        flags |= FLAG_TILT_ALERT;
        ESP_LOGW(TAG, "  ⚠ EGIM ALARMI — SW-520D tetiklendi");
    }

    // ── Step 5: Build own SensorPacket ───────────────────────────────────────
    SensorPacket my_pkt;
    packet_build(&my_pkt, NODE_ID, soil_compen, air_temp_c10, air_humidity,
                 battery_mv, -70 /* guncellenir: sync sonrasi */, 0, flags);
    my_pkt.soil_temp_c10 = soil_temp_c10;
    my_pkt.pressure_pa   = pressure_pa;
    my_pkt.timestamp_ms = (uint32_t)(g_accumulated_time_s * 1000ULL +
                                     (esp_timer_get_time() / 1000));

    // ── Step 6: Neighbor table ────────────────────────────────────────────────
    NeighborTable ntbl;
    neighbor_table_get(&ntbl);
    bool is_relay = !mac_is_zero(ntbl.child_mac);

    // ── Step 7: Setup phase (first-time connectivity handshake) ───────────────
    if (!g_setup_complete) {
        setup_phase_run(&my_pkt, &ntbl);
        // setup_phase_run returns with g_setup_complete=true and ESP-NOW deinitialized.
        // Fall through to normal phase immediately on the same boot.
    }

    // ── Step 8: Normal phase — init ESP-NOW ───────────────────────────────────
    ESP_LOGI(TAG, "─── Haberlesme ──────────────────────────────────");
    ESP_ERROR_CHECK(espnow_init());
    espnow_add_peer(ntbl.parent_mac);
    if (!mac_is_zero(ntbl.skip_mac)) {
        espnow_add_peer(ntbl.skip_mac);
    }
    espnow_register_ping_cb(sync_handle_ping);

    // ── Steps 9–12: Comm cycle ────────────────────────────────────────────────
    SensorPacket child_pkt;
    bool      got_child   = false;
    int8_t    parent_rssi = -70;
    esp_err_t sync_ret, send_ret;

    // Step 9: Relay listen window (Node B only)
    if (is_relay) {
        espnow_add_peer(ntbl.child_mac);
        got_child = relay_listen(&child_pkt, ntbl.child_mac, RELAY_LISTEN_MS);
        if (!got_child) {
            ESP_LOGW(TAG, "  Child paketi alinamadi — sadece kendi verisi gonderilecek");
        }
    }

    // Step 10: Time sync with parent (Cristian's Algorithm)
    sync_ret = sync_ping(ntbl.parent_mac, &parent_rssi);
    if (sync_ret == ESP_OK) {
        tpc_adjust(parent_rssi);
        my_pkt.rssi_dbm = parent_rssi;
    } else {
        ESP_LOGW(TAG, "  Sync basarisiz — drift guncellenmedi");
    }

    // Step 11 & 12: TEST BYPASS LOGIC (No Aggregation)
    if (is_relay && got_child) {
        ESP_LOGI(TAG, "Node 1 verisi dogrudan iletiliyor (Bypass Mode)...");
        
        // Node 1'in paketinin hop (atlama) sayisini artir
        child_pkt.hop_count++;
        packet_print(&child_pkt);
        
        // Node 1'in verisini ham haliyle ilet
        send_ret = relay_send_with_healing(&child_pkt, &ntbl);
        if (send_ret != ESP_OK) {
            ESP_LOGE(TAG, "Node 1'in bypass gonderimi basarisiz oldu");
        }
        
        // Gateway uzerindeki Python scriptinin seri portu bosaltmasi
        // ve ESP-NOW TX kanallarinin rahatlamasi icin ufak bekleme
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    ESP_LOGI(TAG, "Node %d kendi verisini iletiyor...", NODE_ID);
    packet_print(&my_pkt);
    send_ret = relay_send_with_healing(&my_pkt, &ntbl);
    if (send_ret != ESP_OK) {
        ESP_LOGE(TAG, "Kendi verisinin gonderimi basarisiz oldu");
    }

    // ── Step 13: Deinit ESP-NOW ───────────────────────────────────────────────
    espnow_deinit();

    // ── Step 14: Deep sleep ───────────────────────────────────────────────────
    uint64_t sleep_us = sleep_manager_get_duration_us(battery_pct, cycle_start_us);
    rtc_store_update_time(sleep_us);

    ESP_LOGI(TAG, "─── Uyku ────────────────────────────────────────");
    ESP_LOGI(TAG, "  → deep sleep %.2f dk  [boot=%lu  bat=%u%%  saat=%u]",
             sleep_us / 60000000.0f, (unsigned long)g_boot_count,
             battery_pct, sleep_manager_current_hour());
    ESP_LOGI(TAG, "");

    vTaskDelay(pdMS_TO_TICKS(50));   // serial flush
    esp_sleep_enable_timer_wakeup(sleep_us);
    esp_deep_sleep_start();
}
