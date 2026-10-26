#include "setup.h"
#include "node_config.h"
#include "espnow_manager.h"
#include "sync.h"
#include "relay.h"
#include "rtc_store.h"
#include "packet.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SETUP";

// ─── Shared semaphore for SETUP packet reception ──────────────────────────────
static SemaphoreHandle_t s_setup_sem    = NULL;
static SetupPacket       s_last_setup;
static bool              s_setup_ready = false;

static void on_setup_received(const uint8_t src_mac[6], const SetupPacket *pkt, int8_t rssi)
{
    (void)src_mac;
    (void)rssi;
    memcpy(&s_last_setup, pkt, sizeof(SetupPacket));
    s_setup_ready = true;
    if (s_setup_sem) xSemaphoreGive(s_setup_sem);
}

// Wait for a SETUP packet with the given status value.
// Returns true if received within timeout_ms.
static bool wait_for_setup(uint8_t expected_status, uint32_t timeout_ms)
{
    
    s_setup_ready = false;

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (true) {
        TickType_t now       = xTaskGetTickCount();
        TickType_t remaining = (deadline > now) ? (deadline - now) : 0;

        if (remaining == 0) return false;

        if (xSemaphoreTake(s_setup_sem, remaining) == pdFALSE) return false;

        if (s_last_setup.status == expected_status) return true;
        // Wrong status — discard and keep waiting
    }
}

// ─── Node B (Relay) setup phase ───────────────────────────────────────────────
#if NODE_ID == 2
static void setup_relay(SensorPacket *my_pkt, const NeighborTable *ntbl)
{
    int attempt = 0;

    while (!g_setup_complete) {
        attempt++;
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "═══ Setup döngüsü #%d (RELAY) ══════════════════", attempt);

        ESP_ERROR_CHECK(espnow_init());
        espnow_add_peer(ntbl->parent_mac);  // gateway
        espnow_add_peer(ntbl->child_mac);   // node A
        espnow_register_ping_cb(sync_handle_ping);

        if (!s_setup_sem) {
            s_setup_sem = xSemaphoreCreateBinary();
        }
        espnow_register_setup_cb(on_setup_received);

        // ── Step 1: Confirm B → Gateway link ─────────────────────────────────
        ESP_LOGI(TAG, "[SETUP] B→Gateway gönderim deneniyor...");
        esp_err_t ret = espnow_send_data(ntbl->parent_mac, my_pkt, sizeof(SensorPacket));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[SETUP] B→Gateway başarısız (%s), tekrar deneniyor",
                     esp_err_to_name(ret));
            espnow_deinit();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "[SETUP] B→Gateway gönderim başarılı (deneme %d)", attempt);

        // ── Step 2: Sync with gateway ─────────────────────────────────────────
        int8_t rssi = -70;
        esp_err_t sync_ret = sync_ping(ntbl->parent_mac, &rssi);
        if (sync_ret == ESP_OK) {
            ESP_LOGI(TAG, "[SETUP] B→Gateway sync tamamlandı | rssi=%d dBm", rssi);
        } else {
            ESP_LOGW(TAG, "[SETUP] B→Gateway sync başarısız — devam ediliyor");
        }

        // ── Step 3: Listen for A's DATA to confirm A → B link ────────────────
        ESP_LOGI(TAG, "[SETUP] A'dan veri bekleniyor (%lu ms)...",
                 (unsigned long)SETUP_LISTEN_MS);
        SensorPacket child_pkt;
        bool got_a = relay_listen(&child_pkt, ntbl->child_mac, SETUP_LISTEN_MS);
        if (!got_a) {
            ESP_LOGW(TAG, "[SETUP] A'dan veri gelmedi — döngüye dönülüyor");
            espnow_deinit();
            continue;
        }
        ESP_LOGI(TAG, "[SETUP] A'dan veri alındı (node_id=%u)", child_pkt.node_id);

        // ── Step 4: Tell A we're ready ────────────────────────────────────────
        SetupPacket ready_pkt = {
            .type    = PKT_TYPE_SETUP,
            .node_id = NODE_ID,
            .status  = SETUP_READY,
        };
        ret = espnow_send_data(ntbl->child_mac, &ready_pkt, sizeof(SetupPacket));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[SETUP] B→A SETUP_READY gönderilemedi — döngüye dönülüyor");
            espnow_deinit();
            continue;
        }
        ESP_LOGI(TAG, "[SETUP] B→A SETUP_READY gönderildi");

        // ── Step 5: Wait for A's SETUP_SLEEP ─────────────────────────────────
        bool got_sleep = wait_for_setup(SETUP_SLEEP, SETUP_SLEEP_TIMEOUT_MS);
        if (got_sleep) {
            ESP_LOGI(TAG, "[SETUP] SETUP_SLEEP alındı");
            ESP_LOGI(TAG, "[SETUP] Setup tamamlandı → normal döngü başlıyor");
            g_setup_complete = true;
            espnow_deinit();
            return;
        }

        ESP_LOGW(TAG, "[SETUP] SETUP_SLEEP gelmedi (%lu ms timeout) — döngüye dönülüyor",
                 (unsigned long)SETUP_SLEEP_TIMEOUT_MS);
        espnow_deinit();
    }
}
#endif  // NODE_ID == 2

// ─── Node A (Leaf) setup phase ────────────────────────────────────────────────
#if NODE_ID == 1
static void setup_leaf(SensorPacket *my_pkt, const NeighborTable *ntbl)
{
    int attempt = 0;

    while (!g_setup_complete) {
        attempt++;
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "═══ Setup döngüsü #%d (LEAF) ═══════════════════", attempt);

        ESP_ERROR_CHECK(espnow_init());
        espnow_add_peer(ntbl->parent_mac);  // node B
        espnow_register_ping_cb(sync_handle_ping);

        if (!s_setup_sem) {
            s_setup_sem = xSemaphoreCreateBinary();
        }
        espnow_register_setup_cb(on_setup_received);

        // ── Step 1: Send DATA to B ────────────────────────────────────────────
        ESP_LOGI(TAG, "[SETUP] A→B gönderim deneniyor...");
        esp_err_t ret = espnow_send_data(ntbl->parent_mac, my_pkt, sizeof(SensorPacket));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[SETUP] A→B başarısız (%s), tekrar deneniyor",
                     esp_err_to_name(ret));
            espnow_deinit();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "[SETUP] A→B gönderim başarılı (deneme %d)", attempt);

        // ── Step 2: Sync with B ───────────────────────────────────────────────
        int8_t rssi = -70;
        esp_err_t sync_ret = sync_ping(ntbl->parent_mac, &rssi);
        if (sync_ret == ESP_OK) {
            ESP_LOGI(TAG, "[SETUP] A→B sync tamamlandı | rssi=%d dBm", rssi);
        } else {
            ESP_LOGW(TAG, "[SETUP] A→B sync başarısız — devam ediliyor");
        }

        // ── Step 3: Wait for B's SETUP_READY ─────────────────────────────────
        ESP_LOGI(TAG, "[SETUP] B'den SETUP_READY bekleniyor (%lu ms)...",
                 (unsigned long)SETUP_READY_TIMEOUT_MS);
        bool got_ready = wait_for_setup(SETUP_READY, SETUP_READY_TIMEOUT_MS);
        if (!got_ready) {
            ESP_LOGW(TAG, "[SETUP] SETUP_READY gelmedi — döngüye dönülüyor");
            espnow_deinit();
            continue;
        }
        ESP_LOGI(TAG, "[SETUP] SETUP_READY alındı");

        // ── Step 4: Tell B to enter sleep loop ────────────────────────────────
        SetupPacket sleep_pkt = {
            .type    = PKT_TYPE_SETUP,
            .node_id = NODE_ID,
            .status  = SETUP_SLEEP,
        };
        ret = espnow_send_data(ntbl->parent_mac, &sleep_pkt, sizeof(SetupPacket));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[SETUP] A→B SETUP_SLEEP gönderilemedi — yine de devam ediliyor");
        } else {
            ESP_LOGI(TAG, "[SETUP] A→B SETUP_SLEEP gönderildi");
        }

        ESP_LOGI(TAG, "[SETUP] Setup tamamlandı → normal döngü başlıyor");
        g_setup_complete = true;
        espnow_deinit();
        return;
    }
}
#endif  // NODE_ID == 1

// ─── Public entry point ───────────────────────────────────────────────────────
void setup_phase_run(SensorPacket *my_pkt, const NeighborTable *ntbl)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  SETUP PHASE — ilk boot senkronizasyonu      ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

#if NODE_ID == 2
    setup_relay(my_pkt, ntbl);
#elif NODE_ID == 1
    setup_leaf(my_pkt, ntbl);
#endif
}
