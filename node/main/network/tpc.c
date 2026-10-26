#include "tpc.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "TPC";

// Last RSSI value that caused a power change — reset to -999 each boot (acceptable)
static int8_t s_last_rssi = -70;

static int8_t rssi_to_tx_power(int8_t rssi)
{
    if (rssi >= TPC_RSSI_STRONG) return TPC_TX_MIN;
    if (rssi <= TPC_RSSI_WEAK)   return TPC_TX_MAX;

    // Linear interpolation between strong (-40 dBm) and weak (-80 dBm) bounds
    int range_rssi = TPC_RSSI_STRONG - TPC_RSSI_WEAK;                // = 40
    int range_pwr  = TPC_TX_MAX - TPC_TX_MIN;                        // = 34
    int delta_rssi = TPC_RSSI_STRONG - rssi;                         // distance from strong
    return (int8_t)(TPC_TX_MIN + (delta_rssi * range_pwr / range_rssi));
}

void tpc_adjust(int8_t rssi_dbm)
{
    // Hysteresis: only act if RSSI moved more than ±HYSTERESIS from last trigger
    if (abs((int)rssi_dbm - (int)s_last_rssi) < TPC_HYSTERESIS) {
        ESP_LOGD(TAG, "RSSI %d dBm within hysteresis band of %d — no change", rssi_dbm, s_last_rssi);
        return;
    }

    int8_t new_tx = rssi_to_tx_power(rssi_dbm);
    esp_wifi_set_max_tx_power(new_tx);
    ESP_LOGI(TAG, "RSSI %d dBm → TX power %.1f dBm (was %d)", rssi_dbm, new_tx / 4.0f, s_last_rssi);
    s_last_rssi = rssi_dbm;
}

void tpc_set_max(void)
{
    esp_wifi_set_max_tx_power(TPC_TX_MAX);
    s_last_rssi = TPC_RSSI_WEAK - 1;  // force update next call
    ESP_LOGI(TAG, "TX power forced to maximum (%.1f dBm) for self-healing", TPC_TX_MAX / 4.0f);
}
