#include "relay.h"
#include "node_config.h"
#include "espnow_manager.h"
#include "sync.h"
#include "tpc.h"
#include "rtc_store.h"
#include "packet.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "RELAY";

// ─── Listen-window internal state ─────────────────────────────────────────────
static SemaphoreHandle_t s_data_sem   = NULL;
static SensorPacket      s_rx_pkt;
static bool              s_data_ready = false;
static uint8_t           s_expected_child[6];   // only accept data from this MAC

static void on_data_received(const uint8_t src_mac[6], const SensorPacket *pkt, int8_t rssi)
{
    if (memcmp(src_mac, s_expected_child, 6) != 0) {
        ESP_LOGW(TAG, "Paket reddedildi: bilinmeyen kaynak " PRI_MAC, MAC_ARGS(src_mac));
        return;
    }
    ESP_LOGI(TAG, ">>> Paket alindi [ Node %d | soil=%.2f%%  temp=%.1f°C  hum=%u%%  bat=%u mV  rssi=%d dBm ]",
             pkt->node_id,
             pkt->soil_moisture / 100.0f,
             pkt->air_temp_c10  / 10.0f,
             pkt->air_humidity,
             pkt->battery_mv,
             rssi);
    memcpy(&s_rx_pkt, pkt, sizeof(SensorPacket));
    s_rx_pkt.rssi_dbm = rssi;
    s_data_ready = true;
    if (s_data_sem) xSemaphoreGive(s_data_sem);
}

bool relay_listen(SensorPacket *out_pkt, const uint8_t child_mac[6], uint32_t timeout_ms)
{
    if (!s_data_sem) {
        s_data_sem = xSemaphoreCreateBinary();
    }
    espnow_register_data_cb(on_data_received);
    espnow_register_ping_cb(sync_handle_ping);

    memcpy(s_expected_child, child_mac, 6);
    s_data_ready = false;
    xSemaphoreTake(s_data_sem, 0);   // drain any leftover signal from a previous call

    // Handle DATA that arrived before data_cb was registered (Node 1 booted before us)
    {
        uint8_t      early_src[6];
        int8_t       early_rssi = 0;
        SensorPacket early_pkt;
        if (espnow_pop_pending_data(early_src, &early_pkt, &early_rssi)
                && memcmp(early_src, child_mac, 6) == 0) {
            memcpy(&s_rx_pkt, &early_pkt, sizeof(SensorPacket));
            s_rx_pkt.rssi_dbm = early_rssi;
            // Packet arrived before window — use maximum-early correction
            g_child_arrival_offset_us = -((int64_t)timeout_ms * 500LL);
            ESP_LOGI(TAG, ">>> Erken paket alindi [ Node %d | soil=%.2f%%  temp=%.1f°C  hum=%u%%  bat=%u mV  rssi=%d dBm | faz=%+lld ms ]",
                     s_rx_pkt.node_id,
                     s_rx_pkt.soil_moisture / 100.0f,
                     s_rx_pkt.air_temp_c10  / 10.0f,
                     s_rx_pkt.air_humidity,
                     s_rx_pkt.battery_mv,
                     early_rssi,
                     g_child_arrival_offset_us / 1000LL);
            memcpy(out_pkt, &s_rx_pkt, sizeof(SensorPacket));
            return true;
        }
    }

    ESP_LOGI(TAG, "Dinleme penceresi acildi (%lu ms) — child bekleniyor...",
             (unsigned long)timeout_ms);

    int64_t t_window_open = esp_timer_get_time();

    if (xSemaphoreTake(s_data_sem, pdMS_TO_TICKS(timeout_ms)) == pdFALSE) {
        ESP_LOGW(TAG, "Dinleme suresi doldu — child paketi gelmedi");
        // Treat as maximum-late arrival to correct phase toward window center
        g_child_arrival_offset_us = (int64_t)timeout_ms * 500LL;
        return false;
    }

    int64_t arrival_us = esp_timer_get_time() - t_window_open;
    int64_t center_us  = (int64_t)timeout_ms * 500LL;    // RELAY_LISTEN_MS/2 in µs
    g_child_arrival_offset_us = arrival_us - center_us;

    memcpy(out_pkt, &s_rx_pkt, sizeof(SensorPacket));
    return true;
}

// ─── In-network aggregation ───────────────────────────────────────────────────
void relay_aggregate(const SensorPacket *my, const SensorPacket *child, SensorPacket *out)
{
    memcpy(out, my, sizeof(SensorPacket));

    uint8_t total_agg = my->agg_count + child->agg_count;

    // Weighted average for continuous sensor values
    out->soil_moisture = (uint16_t)(
        ((uint32_t)my->soil_moisture * my->agg_count +
         (uint32_t)child->soil_moisture * child->agg_count) / total_agg);

    out->air_temp_c10 = (int16_t)(
        ((int32_t)my->air_temp_c10 * my->agg_count +
         (int32_t)child->air_temp_c10 * child->agg_count) / total_agg);

    out->air_humidity = (uint8_t)(
        ((uint32_t)my->air_humidity * my->agg_count +
         (uint32_t)child->air_humidity * child->agg_count) / total_agg);

    // Battery: report the lowest (worst) value in the chain
    if (child->battery_mv < my->battery_mv) {
        out->battery_mv = child->battery_mv;
    }

    // Flags: OR-merge (propagate all alerts)
    out->flags |= child->flags;

    // Hop count: max in chain + 1 for this relay hop
    uint8_t max_hop = (child->hop_count > my->hop_count) ? child->hop_count : my->hop_count;
    out->hop_count = max_hop + 1;

    // Aggregate counter
    out->agg_count = total_agg;

    // RSSI: keep our own (last hop to parent)
    // out->rssi_dbm stays as my->rssi_dbm (set after sync)

    ESP_LOGI(TAG, "Aggregated %d nodes: soil=%u  temp=%.1f°C  hum=%u%%  bat=%u mV",
             out->agg_count, out->soil_moisture,
             out->air_temp_c10 / 10.0f, out->air_humidity, out->battery_mv);
}

// ─── Self-healing send ────────────────────────────────────────────────────────
esp_err_t relay_send_with_healing(SensorPacket *pkt, const NeighborTable *ntbl)
{
#if ENABLE_SELF_HEALING
    bool use_skip = (g_consecutive_failures >= MAX_RETRIES);
#else
    bool use_skip = false;
#endif

    const uint8_t *target = use_skip ? ntbl->skip_mac : ntbl->parent_mac;
    const char    *label  = use_skip ? "SKIP  " : "PARENT";

    if (use_skip) {
        tpc_set_max();
        pkt->flags |= FLAG_NODE_SKIP;
        ESP_LOGW(TAG, "Self-healing aktif (art arda %d hata) — SKIP hedefi deneniyor",
                 g_consecutive_failures);
    }

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        ESP_LOGI(TAG, "  Gonderim [%d/%d] → %s " PRI_MAC,
                 attempt, MAX_RETRIES, label, MAC_ARGS(target));

        esp_err_t err = espnow_send_data(target, pkt, sizeof(SensorPacket));
        if (err == ESP_OK) {
            g_consecutive_failures = 0;
            ESP_LOGI(TAG, "  ✓ Teslim onaylandi");
            return ESP_OK;
        }
        if (attempt < MAX_RETRIES) vTaskDelay(pdMS_TO_TICKS(200));
    }

#if ENABLE_SELF_HEALING
    // Primary target failed — escalate to skip_mac
    if (!use_skip && !mac_is_zero(ntbl->skip_mac)) {
        ESP_LOGW(TAG, "  Ana hedef %d denemede yanit vermedi — skip_mac'e geciliyor",
                 MAX_RETRIES);
        g_consecutive_failures = MAX_RETRIES;
        tpc_set_max();
        pkt->flags |= FLAG_NODE_SKIP;

        for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
            ESP_LOGI(TAG, "  Gonderim [%d/%d] → SKIP   " PRI_MAC,
                     attempt, MAX_RETRIES, MAC_ARGS(ntbl->skip_mac));

            esp_err_t err = espnow_send_data(ntbl->skip_mac, pkt, sizeof(SensorPacket));
            if (err == ESP_OK) {
                ESP_LOGW(TAG, "  ⚡ Skip teslim basarili — ana dugum down olabilir");
                return ESP_OK;
            }
            if (attempt < MAX_RETRIES) vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
#endif  // ENABLE_SELF_HEALING

    g_consecutive_failures++;
    ESP_LOGE(TAG, "  ✗ Tum gonderim denemeleri basarisiz (art_arda_hata=%d)",
             g_consecutive_failures);
    return ESP_FAIL;
}
