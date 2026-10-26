#include "rtc_store.h"
#include "esp_log.h"

static const char *TAG = "RTC";

// RTC_DATA_ATTR — retained in RTC slow memory across deep sleep
RTC_DATA_ATTR uint32_t g_boot_count           = 0;
RTC_DATA_ATTR uint32_t g_accumulated_time_s   = 0;
RTC_DATA_ATTR int64_t  g_drift_avg_us         = 0;
RTC_DATA_ATTR uint8_t  g_consecutive_failures = 0;
RTC_DATA_ATTR bool     g_setup_complete       = false;

// Per-cycle history for delta-offset drift calculation
RTC_DATA_ATTR int64_t  g_prev_sync_offset_us  = 0;
RTC_DATA_ATTR uint64_t g_prev_active_us       = 0;
RTC_DATA_ATTR uint64_t g_prev_sleep_us        = 0;

// Phase correction: signed offset from window center when child arrived (µs).
// Positive = child arrived late (Node 2 wakes too early → sleep more next cycle).
// Negative = child arrived early (Node 2 wakes too late → sleep less next cycle).
// Set each boot by relay_listen(); zero for leaf nodes.
RTC_DATA_ATTR int64_t  g_child_arrival_offset_us = 0;

void rtc_store_on_boot(void)
{
    g_boot_count++;
    ESP_LOGI(TAG, "Boot #%lu | uptime=%lu s (%luh %02lum) | drift_avg=%+lld µs | art_arda_hata=%u | setup=%s",
             (unsigned long)g_boot_count,
             (unsigned long)g_accumulated_time_s,
             (unsigned long)(g_accumulated_time_s / 3600),
             (unsigned long)((g_accumulated_time_s % 3600) / 60),
             g_drift_avg_us,
             g_consecutive_failures,
             g_setup_complete ? "TAMAM" : "BEKLIYOR");
}

void rtc_store_update_time(uint64_t sleep_us)
{
    g_accumulated_time_s += (uint32_t)(sleep_us / 1000000ULL);
    ESP_LOGI(TAG, "Yazilim saati: %lu s (%luh %02lum %02lus)",
             (unsigned long)g_accumulated_time_s,
             (unsigned long)(g_accumulated_time_s / 3600),
             (unsigned long)((g_accumulated_time_s % 3600) / 60),
             (unsigned long)(g_accumulated_time_s % 60));
}

void rtc_store_save_sync_offset(int64_t offset)
{
    g_prev_sync_offset_us = offset;
}

void rtc_store_save_cycle(uint64_t active_us, uint64_t sleep_us)
{
    g_prev_active_us = active_us;
    g_prev_sleep_us  = sleep_us;
}
