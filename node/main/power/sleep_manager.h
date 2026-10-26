#pragma once
#include <stdint.h>

// Return the next sleep duration in microseconds.
// sleep = target_cycle_period - elapsed_active_time - drift_correction
// This keeps total cycle time (active + sleep) constant across nodes so that
// relative wakeup timing is preserved regardless of how long the active phase was.
//
// battery_pct    : from battery_mv_to_percent()
// cycle_start_us : esp_timer_get_time() captured at the very start of app_main
uint64_t sleep_manager_get_duration_us(uint8_t battery_pct, int64_t cycle_start_us);

// Return the current hour of day derived from g_accumulated_time_s.
// Wraps modulo 24. Example: 6 = 06:00, 21 = 21:00.
uint8_t sleep_manager_current_hour(void);
