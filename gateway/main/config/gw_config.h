#pragma once

// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║                    GATEWAY CONFIGURATION                                     ║
// ║  Edit before flashing.  All other files remain unchanged.                    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

// ─── ESP-NOW channel ─────────────────────────────────────────────────────────
// IMPORTANT: Must match ESPNOW_CHANNEL in node/main/config/node_config.h.
// Gateway does not use Wi-Fi; this fixes the radio channel for ESP-NOW reception.
#define ESPNOW_CHANNEL    1

// ─── Watchdog ────────────────────────────────────────────────────────────────
// Reboot gateway if no packet received for this many minutes (stuck detection)
#define WATCHDOG_TIMEOUT_MIN    30

// ─── Serial output ───────────────────────────────────────────────────────────
// Must match CONFIG_ESP_CONSOLE_UART_BAUDRATE in sdkconfig (idf.py monitor default).
#define SERIAL_BAUD    115200
