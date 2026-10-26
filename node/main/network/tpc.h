#pragma once
#include <stdint.h>

// RSSI thresholds (dBm)
#define TPC_RSSI_STRONG   -40    // above this → minimum TX power
#define TPC_RSSI_WEAK     -80    // below this → maximum TX power (also self-healing)
#define TPC_HYSTERESIS     5     // dBm dead-band — power only changes outside this band

// TX power in ESP-IDF units (dBm × 4)
#define TPC_TX_MIN        44     // 11.0 dBm — close-range / good signal
#define TPC_TX_MAX        78     // 19.5 dBm — long-range / self-healing

// Adjust Wi-Fi TX power based on last-hop RSSI.
// Applies hysteresis: power only changes if RSSI moved outside the ±5 dBm band.
// Call after receiving an ACK (which carries RSSI from parent).
void tpc_adjust(int8_t rssi_dbm);

// Force TX power to maximum (called before self-healing node-skip attempt).
void tpc_set_max(void);
