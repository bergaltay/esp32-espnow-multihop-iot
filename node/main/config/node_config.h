#pragma once

// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║                    PER-DEVICE CONFIGURATION                                  ║
// ║  Edit these values before flashing each node. Everything else is automatic.  ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

// ─── Node identity ────────────────────────────────────────────────────────────
// 0 = Gateway (use gateway/ project instead)
// 1 = Leaf node   (Node A — no children, furthest from gateway)
// 2 = Relay node  (Node B — relays Node A's data toward gateway)
#define NODE_ID    1    // ← CHANGE THIS PER DEVICE

// ─── Sensor mode ─────────────────────────────────────────────────────────────
// 0 = Mock sensors  (no hardware needed — realistic fake data, deep sleep works)
// 1 = Real sensors  (DHT22 + capacitive soil + battery ADC on actual hardware)
#define USE_REAL_SENSORS    1    // ← SET TO 1 WHEN SENSORS ARE CONNECTED

// ─── Battery mode ─────────────────────────────────────────────────────────────
#define USE_REAL_BATTERY    0    // ← SET TO 1 WHEN BATTERY IS CONNECTED


// ─── MAC Address Table ────────────────────────────────────────────────────────
// Fill in after reading each device's MAC with: idf.py monitor → boot log shows MAC
// Format: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
// fill in your device MACs

#define NODE_A_MAC    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}   // Leaf  node (id=1)
#define NODE_B_MAC    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}   // Relay node (id=2)
#define GATEWAY_MAC   {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}   // Gateway    (id=0)

// ─── ESP-NOW channel ─────────────────────────────────────────────────────────
// IMPORTANT: Must match ESPNOW_CHANNEL in gateway/main/config/gw_config.h.
// Gateway does not use Wi-Fi; all nodes and gateway use this fixed channel.
#define ESPNOW_CHANNEL    1

// ─── Timing ──────────────────────────────────────────────────────────────────
#define SLEEP_DURATION_DAY_US     (15ULL * 1000000)
#define SLEEP_DURATION_NIGHT_US   (15ULL * 1000000)
#define DAYTIME_START_HOUR        6
#define DAYTIME_END_HOUR          20

// Relay listen window — Node B stays awake this long waiting for Node A.
// Node A should wake ~2 seconds AFTER Node B. Power them up with that offset.
#define RELAY_LISTEN_MS           6000    // 6 second window (wider coverage for phase correction)

// ─── Self-healing ────────────────────────────────────────────────────────────
// 0 = disabled (prototype default): node only retries parent_mac, no skip_mac fallback
// 1 = enabled: after MAX_RETRIES failures, escalates to skip_mac with FLAG_NODE_SKIP
#define ENABLE_SELF_HEALING  0

#define MAX_RETRIES       3        // consecutive failures before skip escalation (when enabled)
#define SEND_TIMEOUT_MS   1000     // wait for delivery callback
#define ACK_TIMEOUT_MS    500      // wait for sync ACK

// ─── Setup phase timeouts ────────────────────────────────────────────────────
// SETUP_LISTEN_MS: how long Node B waits for Node A's DATA in each setup loop iteration.
// Should be > Node A's boot+sensor time. With 2s power-on offset, 8s is comfortable.
#define SETUP_LISTEN_MS          8000
// SETUP_READY_TIMEOUT_MS: how long Node A waits for B's SETUP_READY after its send.
#define SETUP_READY_TIMEOUT_MS   5000
// SETUP_SLEEP_TIMEOUT_MS: how long Node B waits for A's SETUP_SLEEP after sending READY.
#define SETUP_SLEEP_TIMEOUT_MS   5000

// ─── Battery thresholds ──────────────────────────────────────────────────────
#define BROWNOUT_THRESHOLD_MV    100    // below this → skip sensors + radio
#define FROST_THRESHOLD_C10      20      // 2.0°C × 10 — frost alert trigger

// ─── Sensor hardware ─────────────────────────────────────────────────────────
#define MOSFET_STABILIZE_MS      100     // wait after MOSFET on before reading
#define TEMP_COMPENSATION_REF    25.0f   // moisture compensation reference temp (°C)
#define TEMP_COMPENSATION_COEFF  0.3f    // correction per °C (calibrate per soil type)

// ─── Mock sensor values (USE_REAL_SENSORS == 0) ───────────────────────────────
// Realistic defaults for a summer day in central Anatolia.
// Boot-count-based variation is added in each driver for realism.
#define MOCK_BATTERY_MV        3750     // ~50% charge
#define MOCK_AIR_TEMP_C10      225      // 22.5°C base
#define MOCK_AIR_HUMIDITY      65       // 65% RH base
#define MOCK_SOIL_MOISTURE_RAW 3000     // moderate moisture (ADC units)
#define MOCK_SOIL_TEMP_C10     185      // 18.5°C soil temperature
#define MOCK_PRESSURE_PA       101325   // ~1 atm sea-level pressure
