#pragma once
#include <esp_wifi.h>

// ─── Nom de cet appareil (visible dans la page web) ──────────────────────────
// Changer pour chaque ESP32-C3 : "Boutons_1", "Boutons_2", etc.
#define DEVICE_NAME     "Boutons_1"

// ─── Pins des boutons (INPUT_PULLUP, relier à GND) ───────────────────────────
#define BTN_COUNT   6
static const uint8_t BTN_PINS[BTN_COUNT] = { 2, 3, 4, 5, 6, 7 };

// ─── Timing ───────────────────────────────────────────────────────────────────
#define LONG_PRESS_MS   800
#define DEBOUNCE_MS     30

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
// Si le serveur est déjà connu, remplir son MAC ici.
// Sinon, laisser FF:FF:FF:FF:FF:FF et utiliser le mode appairage sur la page web.
#define SERVER_MAC { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
