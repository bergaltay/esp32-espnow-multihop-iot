#include "sleep_manager.h"
#include "node_config.h"
#include "rtc_store.h"
#include "sync.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "SLEEP";

// Clamp drift correction to ±30% of target period to prevent runaway
#define MAX_DRIFT_CORRECTION_US  (SLEEP_DURATION_DAY_US * 30 / 100)

uint8_t sleep_manager_current_hour(void)
{
    return (uint8_t)((g_accumulated_time_s / 3600) % 24);
}

uint64_t sleep_manager_get_duration_us(uint8_t battery_pct, int64_t cycle_start_us)
{
    // How long did the active phase take?
    uint64_t elapsed_us = (uint64_t)(esp_timer_get_time() - cycle_start_us);

    uint8_t     hour = sleep_manager_current_hour();
    uint64_t  target_us;
    const char *mode_str;
    if (hour >= DAYTIME_START_HOUR && hour < DAYTIME_END_HOUR) {
        target_us = SLEEP_DURATION_DAY_US;
        mode_str  = "GUNDUZ";
    } else {
        target_us = SLEEP_DURATION_NIGHT_US;
        mode_str  = "GECE  ";
    }

    if (battery_pct < 10) {
        ESP_LOGW(TAG, "Batarya kritik (%u%%) — maksimum uyku suresi", battery_pct);
        return SLEEP_DURATION_NIGHT_US;
    }

    // Cristian's drift correction (clamped to ±30% of target)
    int64_t drift_us = sync_get_drift_us();
    if (drift_us >  (int64_t)MAX_DRIFT_CORRECTION_US) drift_us =  (int64_t)MAX_DRIFT_CORRECTION_US;
    if (drift_us < -(int64_t)MAX_DRIFT_CORRECTION_US) drift_us = -(int64_t)MAX_DRIFT_CORRECTION_US;

    // sleep = target_cycle_period - elapsed_active_time - drift_correction
    //
    // Her iki node da aynı target_cycle_period'u kullanırsa, aktif süreleri
    // ne kadar farklı olursa olsun toplam döngü süreleri eşit kalır.
    // Bu sayede başlangıçtaki faz farkı (Node B 2s önce açılır) tüm
    // döngülerde korunur — Node A her zaman Node B'nin listen penceresine denk gelir.
    int64_t sleep = (int64_t)target_us - (int64_t)elapsed_us - drift_us;

    // Phase correction (relay only): keep child's arrival at window center.
    // g_child_arrival_offset_us > 0: child arrived late → sleep more → Node B wakes later → child arrives earlier.
    // g_child_arrival_offset_us < 0: child arrived early → sleep less → Node B wakes earlier → child arrives later.
    // Leaf nodes always have g_child_arrival_offset_us == 0 → no effect.
    if (g_child_arrival_offset_us != 0) {
        sleep += g_child_arrival_offset_us;
        ESP_LOGI(TAG, "Faz duzeltme: %+lld ms -> uyku guncellendi", g_child_arrival_offset_us / 1000LL);
    }

    // Hard floor: aktif süre periyodu aşsa bile en az 5 s uyu
    if (sleep < 5000000LL) {
        ESP_LOGW(TAG, "Aktif sure periyodu asti (aktif=%.1f s, hedef=%.1f s) — min 5 s uyku",
                 elapsed_us / 1e6f, target_us / 1e6f);
        sleep = 5000000LL;
    }

    ESP_LOGI(TAG, "%s (saat=%u) | hedef=%.2f dk  aktif=%.1f s  drift=%+lld us  faz=%+lld ms  -> uyku=%.2f dk",
             mode_str, hour,
             target_us  / 60000000.0f,
             elapsed_us / 1000000.0f,
             -drift_us,
             g_child_arrival_offset_us / 1000LL,
             sleep      / 60000000.0f);

    rtc_store_save_cycle(elapsed_us, (uint64_t)sleep);
    return (uint64_t)sleep;
}
