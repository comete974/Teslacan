#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "../../shared/protocol.h"
#include "config.h"

// ─── Button definitions ───────────────────────────────────────────────────────
// Short press → first action, long press → second action
struct ButtonDef {
    uint8_t  pin;
    MsgType  short_cmd;
    MsgType  long_cmd;
    const char *label_short;
    const char *label_long;
};

static const ButtonDef buttons[BTN_COUNT] = {
    // pin              short                       long                        label_short          label_long
    { BTN_PIN_0, MsgType::CMD_BEEP_TOGGLE,    MsgType::CMD_HAZARD_TOGGLE,   "Beep mute",        "Hazard lights"  },
    { BTN_PIN_1, MsgType::CMD_SENTINEL_TOGGLE,MsgType::CMD_SENTRY_LIGHTS,   "Sentinel",         "Sentry lights"  },
    { BTN_PIN_2, MsgType::CMD_TRUNK_OPEN,     MsgType::CMD_TRUNK_CLOSE,     "Trunk open",       "Trunk close"    },
    { BTN_PIN_3, MsgType::CMD_FRUNK_TOGGLE,   MsgType::CMD_LOCK,            "Frunk",            "Lock"           },
    { BTN_PIN_4, MsgType::CMD_CLIMATE_ON,     MsgType::CMD_CLIMATE_OFF,     "Climate on",       "Climate off"    },
    { BTN_PIN_5, MsgType::CMD_VOLUME_UP,      MsgType::CMD_WIPER_UP,        "Volume up",        "Wiper up"       },
};

// ─── Runtime state per button ─────────────────────────────────────────────────
struct BtnState {
    bool     last_pressed = false;
    uint32_t press_time   = 0;
    bool     long_fired   = false;
};

static BtnState btn_state[BTN_COUNT];

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
static uint8_t server_mac[6] = SERVER_MAC;

static void send_cmd(MsgType type, uint8_t param = 0) {
    CommandMsg msg;
    msg.type  = type;
    msg.param = param;
    esp_err_t err = esp_now_send(server_mac, (const uint8_t *)&msg, sizeof(msg));
    if (err != ESP_OK) {
        Serial.printf("[ESP-NOW] Send failed: 0x%X\n", err);
    }
}

static void on_espnow_recv(const uint8_t *mac, const uint8_t *data, int len) {
    // Buttons can receive telemetry for LED feedback if desired (future use)
    (void)mac; (void)data; (void)len;
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("=== TeslaCAN Buttons ===");

    // Configure button pins
    for (int i = 0; i < BTN_COUNT; i++) {
        pinMode(buttons[i].pin, INPUT_PULLUP);
    }

    // Print button map to Serial
    Serial.println("Button map:");
    for (int i = 0; i < BTN_COUNT; i++) {
        Serial.printf("  BTN%d (GPIO%d): short='%s'  long='%s'\n",
                      i, buttons[i].pin,
                      buttons[i].label_short, buttons[i].label_long);
    }

    // Init ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.printf("My MAC: %s\n", WiFi.macAddress().c_str());

    esp_now_init();
    esp_now_register_recv_cb(on_espnow_recv);

    esp_now_peer_info_t peer = {};
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    memcpy(peer.peer_addr, server_mac, 6);
    esp_now_add_peer(&peer);

    Serial.println("Buttons ready.");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    for (int i = 0; i < BTN_COUNT; i++) {
        bool pressed = (digitalRead(buttons[i].pin) == LOW);
        BtnState &s  = btn_state[i];

        if (pressed && !s.last_pressed) {
            // Falling edge — start timing
            s.press_time = now;
            s.long_fired = false;
        }

        if (pressed && s.last_pressed && !s.long_fired) {
            // Still held — check for long press threshold
            if ((now - s.press_time) >= LONG_PRESS_MS) {
                Serial.printf("[BTN%d] Long press → %s\n", i, buttons[i].label_long);
                send_cmd(buttons[i].long_cmd);
                s.long_fired = true;
            }
        }

        if (!pressed && s.last_pressed) {
            // Rising edge
            uint32_t held = now - s.press_time;
            if (held >= DEBOUNCE_MS && !s.long_fired) {
                Serial.printf("[BTN%d] Short press → %s\n", i, buttons[i].label_short);
                send_cmd(buttons[i].short_cmd);
            }
        }

        s.last_pressed = pressed;
    }

    delay(10);
}
