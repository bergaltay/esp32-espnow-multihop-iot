#pragma once
#include "packet.h"
#include "neighbor_table.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// ─── Relay listen window ──────────────────────────────────────────────────────
// Open a receive window for `timeout_ms`.  During this window:
//   - Incoming PINGs from child_mac are answered with ACK (via sync_handle_ping)
//   - Incoming DATA from child_mac is stored in out_pkt
// Returns true if a DATA packet arrived before timeout.
bool relay_listen(SensorPacket *out_pkt, const uint8_t child_mac[6], uint32_t timeout_ms);

// ─── In-network data aggregation ─────────────────────────────────────────────
// Merge child packet into parent's own data.  Result goes into merged_out.
// Weighted-average for continuous values, OR-merge for flags, min for battery.
void relay_aggregate(const SensorPacket *my_pkt,
                     const SensorPacket *child_pkt,
                     SensorPacket       *merged_out);

// ─── Self-healing send ────────────────────────────────────────────────────────
// Try to deliver pkt to parent_mac up to MAX_RETRIES times.
// On MAX_RETRIES consecutive failures:
//   - Sets TX power to max
//   - Sets FLAG_NODE_SKIP
//   - Retries up to MAX_RETRIES times via skip_mac
//   - Resets or increments g_consecutive_failures in RTC
// Returns ESP_OK if any delivery succeeded.
esp_err_t relay_send_with_healing(SensorPacket *pkt, const NeighborTable *ntbl);
