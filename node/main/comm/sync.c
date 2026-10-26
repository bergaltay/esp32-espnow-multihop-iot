#include "sync.h"
#include "node_config.h"
#include "espnow_manager.h"
#include "rtc_store.h"
#include "packet.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SYNC";

// EWMA alpha: α = 2/10 = 0.2  (integer arithmetic to avoid float in RTC)
#define EWMA_ALPHA_NUM   2
#define EWMA_ALPHA_DEN   10

static SemaphoreHandle_t s_ack_sem   = NULL;
static AckPacket         s_last_ack;
static bool              s_ack_ready = false;

// ─── ACK receive callback ─────────────────────────────────────────────────────
static void on_ack_received(const AckPacket *ack)
{
    memcpy(&s_last_ack, ack, sizeof(AckPacket));
    s_ack_ready = true;
    if (s_ack_sem) xSemaphoreGive(s_ack_sem);
}

// ─── Public: send PING, wait for ACK, update drift ───────────────────────────
esp_err_t sync_ping(const uint8_t parent_mac[6], int8_t *out_rssi)
{
    if (!s_ack_sem) {
        s_ack_sem = xSemaphoreCreateBinary();
    }
    espnow_register_ack_cb(on_ack_received);
    s_ack_ready = false;

    PingPacket ping = {
        .type         = PKT_TYPE_PING,
        .node_id      = NODE_ID,
        .timestamp_us = esp_timer_get_time(),
    };
    int64_t T_send = ping.timestamp_us;

    esp_err_t err = espnow_send_data(parent_mac, &ping, sizeof(PingPacket));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PING send failed: %s", esp_err_to_name(err));
        return err;
    }

    // Wait for ACK (ACK is sent fire-and-forget by parent, no delivery callback there)
    if (xSemaphoreTake(s_ack_sem, pdMS_TO_TICKS(ACK_TIMEOUT_MS)) == pdFALSE) {
        ESP_LOGW(TAG, "  ACK zaman asimi (%d ms) — parent yanit vermedi", ACK_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }

    int64_t T_recv_ack = esp_timer_get_time();
    int64_t T_remote   = s_last_ack.local_ts_us;   // parent's timestamp when PING arrived
    int64_t rtt        = T_recv_ack - T_send;
    int64_t offset     = T_remote - T_send - rtt / 2;

    // Delta-offset method: measure actual crystal error per sleep cycle.
    // crystal_error = (offset_now - offset_prev) - active_prev - sleep_prev
    //               = sleep_actual - sleep_programmed  (≈ ±few ms)
    // Guard: g_prev_sleep_us == 0 on first boot or after setup phase → skip update.
    int64_t crystal_error = 0;
    if (g_prev_sleep_us > 0) {
        int64_t delta_offset = offset - g_prev_sync_offset_us;
        crystal_error = delta_offset
                        - (int64_t)g_prev_active_us
                        - (int64_t)g_prev_sleep_us;
        // Sanity clamp: real crystal drift < ±500 ms per cycle; larger = anomaly, skip
        if (crystal_error > -500000LL && crystal_error < 500000LL) {
            g_drift_avg_us = (EWMA_ALPHA_NUM * crystal_error
                             + (EWMA_ALPHA_DEN - EWMA_ALPHA_NUM) * g_drift_avg_us)
                             / EWMA_ALPHA_DEN;
        } else {
            ESP_LOGW(TAG, "  Drift anomalisi atildi: crystal_error=%+lld µs", crystal_error);
        }
    }
    rtc_store_save_sync_offset(offset);

    if (out_rssi) *out_rssi = s_last_ack.rssi_dbm;

    ESP_LOGI(TAG, "  Sync OK | rtt=%lld µs  offset=%+lld µs  crystal_err=%+lld µs  drift_avg=%+lld µs  rssi=%d dBm",
             rtt, offset, crystal_error, g_drift_avg_us, s_last_ack.rssi_dbm);
    return ESP_OK;
}

// ─── Public: handle incoming PING from a child, reply with ACK ───────────────
void sync_handle_ping(const uint8_t src_mac[6], const PingPacket *ping, int8_t rssi)
{
    AckPacket ack = {
        .type        = PKT_TYPE_ACK,
        .node_id     = NODE_ID,
        .sender_ts_us = ping->timestamp_us,        // echo back so sender can compute RTT
        .local_ts_us  = esp_timer_get_time(),      // our local time when PING arrived
        .rssi_dbm    = rssi,
    };

    // Dynamically add src as peer if not yet known (needed to send ACK)
    espnow_add_peer(src_mac);
    espnow_send_ack_nowait(src_mac, &ack);

    ESP_LOGD(TAG, "Replied ACK to node " PRI_MAC " (rssi=%d)", MAC_ARGS(src_mac), rssi);
}

int64_t sync_get_drift_us(void)
{
    return g_drift_avg_us;
}
