#pragma once
#include <stdint.h>
#include <stdbool.h>

// Returns true if battery_mv < BROWNOUT_THRESHOLD_MV.
// Sets FLAG_BROWNOUT in out_flags if returning true.
// Call immediately after battery_read_mv(), before any MOSFET/ESP-NOW operations.
bool brownout_check(uint16_t battery_mv);
