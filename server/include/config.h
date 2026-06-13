#pragma once

// ─── CAN bus pins (ESP32 WROOM32 + SN65HVD230) ───────────────────────────────
#define CAN_TX_PIN  21
#define CAN_RX_PIN  22

// ─── CAN bus speed ────────────────────────────────────────────────────────────
// Tesla uses 500 kbps on the OBD2 port
// Use TWAI_TIMING_CONFIG_500KBITS() in code

// ─── ESP-NOW peer MAC addresses ───────────────────────────────────────────────
// Flash each board and read its MAC from Serial ("My MAC: XX:XX:XX:XX:XX:XX")
// then fill in below.

// Display ESP32-S3 MAC — PLACEHOLDER
#define DISPLAY_MAC { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

// Buttons ESP32-C3 MAC — PLACEHOLDER
#define BUTTONS_MAC { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

// WiFi channel for ESP-NOW (must match on all boards)
#define ESPNOW_CHANNEL 1

// ─── Behaviour ────────────────────────────────────────────────────────────────
// Speed above which we consider the car "driving" and trigger auto-mute
#define AUTO_MUTE_SPEED_KMH     2.0f

// Telemetry broadcast rate
#define TELEMETRY_INTERVAL_MS   100     // 10 Hz
