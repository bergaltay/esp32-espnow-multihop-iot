#pragma once
#include "esp_err.h"
#include <stdint.h>
#include "packet.h"

// Send a PING to parent_mac and wait for ACK.
// On success:
//   - g_drift_avg_us is updated (EWMA filtered)
//   - out_rssi is set to the RSSI reported by parent in ACK
// On timeout: g_drift_avg_us unchanged, out_rssi unchanged.
esp_err_t sync_ping(const uint8_t parent_mac[6], int8_t *out_rssi);

// Called by the PING handler (registered with espnow_manager) when a PING
// arrives *at this node* from a child. Builds and fires an ACK back immediately.
// This is the "receiver side" of Cristian's algorithm.
void sync_handle_ping(const uint8_t src_mac[6], const PingPacket *ping, int8_t rssi);

// Access the current drift estimate (µs). Negative = we woke early, positive = late.
// Used by sleep_manager to correct next sleep duration.
int64_t sync_get_drift_us(void);
