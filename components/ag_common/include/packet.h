#pragma once
#include <stdint.h>
#include <stdbool.h>

// ─── Packet type discriminator (first byte of every ESP-NOW frame) ───────────
#define PKT_TYPE_PING   0x01
#define PKT_TYPE_ACK    0x02
#define PKT_TYPE_DATA   0x03
#define PKT_TYPE_SETUP  0x04   // Setup phase coordination packet

// ─── SetupPacket status values ────────────────────────────────────────────────
#define SETUP_READY  0x01   // B → A: "gateway connection confirmed, I'm ready"
#define SETUP_SLEEP  0x02   // A → B: "my connection confirmed, entering sleep loop"

// ─── SensorPacket flags ───────────────────────────────────────────────────────
#define FLAG_FROST_ALERT  0x01
#define FLAG_BROWNOUT     0x02
#define FLAG_NODE_SKIP    0x04
#define FLAG_TILT_ALERT   0x08   // V2 placeholder

// ─── V2 placeholder sentinels ─────────────────────────────────────────────────
#define SOIL_TEMP_INVALID  0x7FFF
#define PRESSURE_INVALID   0

// ─── Main sensor data packet (40 bytes, well within ESP-NOW 250B limit) ──────
typedef struct __attribute__((packed)) {
    uint8_t  type;              // 1B  — PKT_TYPE_DATA (0x03); must be first byte for recv_cb dispatch
    uint8_t  node_id;           // 1B  — unique node identifier
    uint32_t timestamp_ms;      // 4B  — software clock (accumulated sleep time)
    uint16_t soil_moisture;     // 2B  — compensated moisture ×100
    int16_t  air_temp_c10;      // 2B  — air temperature ×10 (DHT22), e.g. 225 = 22.5°C
    uint8_t  air_humidity;      // 1B  — relative humidity % (DHT22)
    int16_t  soil_temp_c10;     // 2B  — V2: DS18B20, set SOIL_TEMP_INVALID in V1
    uint32_t pressure_pa;       // 4B  — V2: BMP280, set PRESSURE_INVALID in V1
    uint16_t battery_mv;        // 2B  — battery voltage in millivolts
    int8_t   rssi_dbm;          // 1B  — last-hop RSSI
    uint8_t  hop_count;         // 1B  — hops traversed in chain
    uint8_t  flags;             // 1B  — FLAG_* bitmask
    uint8_t  fec_parity[5];     // 5B  — reserved for Hamming FEC (Phase 3.3), zeroed for now
    uint8_t  agg_count;         // 1B  — how many nodes' data is aggregated here
    uint8_t  reserved[12];      // 12B — future expansion (was 13B; 1B moved to type field)
} SensorPacket;
// Compile-time size assertion handled in packet.c

// ─── Time synchronisation packets (Cristian's Algorithm) ─────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  type;              // 1B  — PKT_TYPE_PING (0x01)
    uint8_t  node_id;           // 1B
    int64_t  timestamp_us;      // 8B  — sender's esp_timer_get_time() at send moment
} PingPacket;                   // 10 bytes total

typedef struct __attribute__((packed)) {
    uint8_t  type;              // 1B  — PKT_TYPE_ACK (0x02)
    uint8_t  node_id;           // 1B
    int64_t  sender_ts_us;      // 8B  — echoed from PING (so sender can compute RTT)
    int64_t  local_ts_us;       // 8B  — receiver's local time when PING was received
    int8_t   rssi_dbm;          // 1B  — RSSI of the incoming PING (for TPC)
} AckPacket;                    // 19 bytes total

// ─── Safe MAC address formatting for ESP_LOG* macros ─────────────────────────
// MACSTR + MAC2STR break inside ESP_LOGX due to macro expansion order.
// Use PRI_MAC + MAC_ARGS instead.
// Example:  ESP_LOGI(TAG, "Peer: " PRI_MAC, MAC_ARGS(mac));
#define PRI_MAC       "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ARGS(m)   (unsigned)(m)[0], (unsigned)(m)[1], (unsigned)(m)[2], \
                      (unsigned)(m)[3], (unsigned)(m)[4], (unsigned)(m)[5]

// ─── Setup phase coordination packet ─────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t type;               // 1B  — PKT_TYPE_SETUP (0x04)
    uint8_t node_id;            // 1B  — sender node id
    uint8_t status;             // 1B  — SETUP_READY or SETUP_SLEEP
} SetupPacket;                  // 3 bytes total

// ─── packet.c public API ──────────────────────────────────────────────────────
void packet_build(SensorPacket *pkt,
                  uint8_t node_id,
                  uint16_t soil_moisture,
                  int16_t  air_temp_c10,
                  uint8_t  air_humidity,
                  uint16_t battery_mv,
                  int8_t   rssi_dbm,
                  uint8_t  hop_count,
                  uint8_t  flags);

void packet_print(const SensorPacket *pkt);

// Returns true if mac is all-zeros (unset)
bool mac_is_zero(const uint8_t mac[6]);
