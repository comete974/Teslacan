#pragma once

// ─── Button GPIOs (ESP32-C3) ──────────────────────────────────────────────────
// Wiring: each button connects GPIO to GND (INPUT_PULLUP).
// Adjust pin numbers to match your PCB/breadboard layout.

#define BTN_COUNT       6

#define BTN_PIN_0       2   // Button 0 — Beep mute toggle
#define BTN_PIN_1       3   // Button 1 — Sentinel toggle
#define BTN_PIN_2       4   // Button 2 — Trunk
#define BTN_PIN_3       5   // Button 3 — Frunk / Lock
#define BTN_PIN_4       6   // Button 4 — Climate / Hazard
#define BTN_PIN_5       7   // Button 5 — Volume / Wiper

// Long press threshold (ms)
#define LONG_PRESS_MS   800

// Debounce time (ms)
#define DEBOUNCE_MS     30

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
#define ESPNOW_CHANNEL  1

// Server MAC — fill after reading server Serial output
#define SERVER_MAC { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
