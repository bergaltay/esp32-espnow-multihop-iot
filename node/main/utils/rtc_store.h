#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"

// ─── RTC memory — survives deep sleep, reset only on power-on ────────────────
// These are defined in rtc_store.c and declared extern here so all modules
// can read them. Only rtc_store.c writes them.

extern uint32_t g_boot_count;           // total wake cycles since power-on
extern uint32_t g_accumulated_time_s;   // software clock: sum of all sleep durations
extern int64_t  g_drift_avg_us;         // EWMA crystal drift estimate (per-cycle delta)
extern uint8_t  g_consecutive_failures; // consecutive send failures (for self-healing)
extern bool     g_setup_complete;       // true once setup phase handshake finishes

// Per-cycle history for delta-offset drift calculation (see sync.c)
extern int64_t  g_prev_sync_offset_us;  // Cristian offset from last successful sync
extern uint64_t g_prev_active_us;       // total active time of the previous boot
extern uint64_t g_prev_sleep_us;        // sleep duration programmed in the previous boot

// Phase correction: signed µs from window center when child DATA arrived (see relay.c)
extern int64_t  g_child_arrival_offset_us;

// Called once at the very start of app_main()
void rtc_store_on_boot(void);

// Called just before entering deep sleep — adds sleep_us to g_accumulated_time_s
void rtc_store_update_time(uint64_t sleep_us);

// Called from sync.c after a successful sync — saves offset for next boot's delta calc
void rtc_store_save_sync_offset(int64_t offset);

// Called from sleep_manager.c before deep sleep — saves timing data for next boot
void rtc_store_save_cycle(uint64_t active_us, uint64_t sleep_us);
