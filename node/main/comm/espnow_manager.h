#pragma once
#include "esp_err.h"
#include "packet.h"
#include <stdint.h>
#include <stdbool.h>

// Initialise Wi-Fi (STA, no AP connection) + ESP-NOW.
// Must be called AFTER all ADC reads.
esp_err_t espnow_init(void);

// Clean shutdown — call before deep sleep.
void espnow_deinit(void);

// Add a peer (idempotent: silently returns if already added).
esp_err_t espnow_add_peer(const uint8_t mac[6]);

// Send raw bytes to peer. Blocks until delivery callback fires (or timeout).
// Returns ESP_OK on confirmed delivery.
esp_err_t espnow_send_data(const uint8_t mac[6], const void *data, size_t len);

// Fire-and-forget — used for ACK responses from receive callback.
// Does NOT wait for delivery confirmation.
void espnow_send_ack_nowait(const uint8_t mac[6], const AckPacket *ack);

// ── Receive event callbacks — registered by other modules ────────────────────
// Called when a PING arrives: module should send ACK immediately.
typedef void (*espnow_ping_cb_t)(const uint8_t src_mac[6], const PingPacket *ping, int8_t rssi);
// Called when an ACK arrives (response to our own PING).
typedef void (*espnow_ack_cb_t)(const AckPacket *ack);
// Called when a DATA packet arrives (child's sensor data for relay).
typedef void (*espnow_data_cb_t)(const uint8_t src_mac[6], const SensorPacket *pkt, int8_t rssi);
// Called when a SETUP packet arrives (setup phase coordination).
typedef void (*espnow_setup_cb_t)(const uint8_t src_mac[6], const SetupPacket *pkt, int8_t rssi);

void espnow_register_ping_cb(espnow_ping_cb_t cb);
void espnow_register_ack_cb(espnow_ack_cb_t cb);
void espnow_register_data_cb(espnow_data_cb_t cb);
void espnow_register_setup_cb(espnow_setup_cb_t cb);

// Pop a DATA packet that arrived before data_cb was registered (early arrival).
// Returns true and fills outputs if a buffered packet exists; false otherwise.
bool espnow_pop_pending_data(uint8_t src_mac[6], SensorPacket *out_pkt, int8_t *out_rssi);
