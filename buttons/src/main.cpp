#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "../../shared/protocol.h"
#include "config.h"

// ─── Button runtime state ─────────────────────────────────────────────────────
struct BtnState {
    bool     last_pressed = false;
    uint32_t press_time   = 0;
    bool     long_fired   = false;
};

static BtnState btn_state[BTN_COUNT];

// ─── Dynamic config (received from server) ────────────────────────────────────
static uint8_t  device_id    = 0xFF;   // assigned by server at pairing
static bool     paired        = false;
static BtnMap   btn_map[BTN_COUNT];    // short/long cmd per button

static Preferences prefs;

static void load_config_from_nvs() {
    prefs.begin("buttons", false);
    device_id = prefs.getUChar("dev_id", 0xFF);
    paired    = (device_id != 0xFF);
    prefs.getBytes("btn_map", btn_map, sizeof(btn_map));
    prefs.end();
    if (paired) Serial.printf("[Config] Loaded from NVS, device_id=%d\n", device_id);
}

static void save_config_to_nvs() {
    prefs.begin("buttons", false);
    prefs.putUChar("dev_id", device_id);
    prefs.putBytes("btn_map", btn_map, sizeof(btn_map));
    prefs.end();
    Serial.println("[Config] Saved to NVS");
}

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
static uint8_t server_mac[6] = SERVER_MAC;

static void send_button_event(uint8_t btn_index, uint8_t press_type) {
    if (!paired) return;
    ButtonEventMsg ev;
    ev.device_id  = device_id;
    ev.btn_index  = btn_index;
    ev.press_type = press_type;
    esp_now_send(server_mac, (const uint8_t *)&ev, sizeof(ev));
}

static void send_hello() {
    HelloMsg h;
    strncpy(h.name, DEVICE_NAME, sizeof(h.name) - 1);
    h.btn_count = BTN_COUNT;
    // Send to broadcast to be discovered, and also directly to server if MAC known
    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_send(bcast, (const uint8_t *)&h, sizeof(h));
    // Also send directly to server MAC
    bool server_placeholder = true;
    for (int i = 0; i < 6; i++) if (server_mac[i] != 0xFF) { server_placeholder=false; break; }
    if (!server_placeholder) {
        esp_now_send(server_mac, (const uint8_t *)&h, sizeof(h));
    }
    Serial.printf("[ESP-NOW] HELLO sent as '%s'\n", DEVICE_NAME);
}

static void on_espnow_recv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < 1) return;
    MsgType type = (MsgType)data[0];

    if (type == MsgType::MSG_HELLO_ACK && len >= (int)sizeof(HelloAckMsg)) {
        const HelloAckMsg *ack = (const HelloAckMsg *)data;
        if (ack->accepted) {
            device_id = ack->device_id;
            paired    = true;
            // Store server MAC from the reply
            memcpy(server_mac, mac, 6);
            save_config_to_nvs();
            Serial.printf("[Pair] Accepted! device_id=%d\n", device_id);
        } else {
            Serial.println("[Pair] Server not in pairing mode.");
        }
        return;
    }

    if (type == MsgType::MSG_BTN_CFG && len >= (int)sizeof(BtnCfgMsg)) {
        const BtnCfgMsg *cfg = (const BtnCfgMsg *)data;
        if (cfg->device_id == device_id) {
            int n = min((int)cfg->btn_count, BTN_COUNT);
            for (int i = 0; i < n; i++) btn_map[i] = cfg->map[i];
            save_config_to_nvs();
            Serial.printf("[Config] Received config: %d buttons\n", n);
            for (int i = 0; i < n; i++) {
                Serial.printf("  BTN%d: short=0x%02X long=0x%02X\n",
                              i, btn_map[i].short_cmd, btn_map[i].long_cmd);
            }
        }
        return;
    }

    // Telemetry from server (optional — for LED feedback in future)
    if (type == MsgType::TELEMETRY) {
        // placeholder: could drive LEDs here
    }
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("=== TeslaCAN Buttons '%s' ===\n", DEVICE_NAME);

    for (int i = 0; i < BTN_COUNT; i++)
        pinMode(BTN_PINS[i], INPUT_PULLUP);

    load_config_from_nvs();

    // If not paired yet, set default map
    if (!paired) {
        static const uint8_t def_short[] = {0x10,0x11,0x12,0x14,0x17,0x19};
        static const uint8_t def_long[]  = {0x1D,0x22,0x13,0x15,0x18,0x1B};
        for (int i = 0; i < BTN_COUNT && i < 6; i++) {
            btn_map[i].short_cmd = def_short[i];
            btn_map[i].long_cmd  = def_long[i];
        }
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // Force channel 1 to match server AP
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    Serial.printf("My MAC: %s\n", WiFi.macAddress().c_str());

    // Register broadcast peer for HELLO
    esp_now_init();
    esp_now_register_recv_cb(on_espnow_recv);

    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_peer_info_t peer = {};
    peer.channel = 1;
    peer.encrypt = false;
    memcpy(peer.peer_addr, bcast, 6);
    esp_now_add_peer(&peer);

    // Register server MAC if known
    bool server_placeholder = true;
    for (int i = 0; i < 6; i++) if (server_mac[i] != 0xFF) { server_placeholder=false; break; }
    if (!server_placeholder) {
        memcpy(peer.peer_addr, server_mac, 6);
        esp_now_add_peer(&peer);
    }

    // Announce ourselves — server will reply if in pairing mode (or re-send config if already paired)
    send_hello();

    Serial.println("Buttons ready.");
    Serial.printf("Paired: %s  device_id: %d\n", paired?"yes":"no (press pair on web)", device_id);
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    for (int i = 0; i < BTN_COUNT; i++) {
        bool pressed = (digitalRead(BTN_PINS[i]) == LOW);
        BtnState &s  = btn_state[i];

        if (pressed && !s.last_pressed) {
            s.press_time = now;
            s.long_fired = false;
        }

        if (pressed && s.last_pressed && !s.long_fired &&
            (now - s.press_time) >= LONG_PRESS_MS) {
            Serial.printf("[BTN%d] Long press → 0x%02X\n", i, btn_map[i].long_cmd);
            send_button_event(i, 1);
            s.long_fired = true;
        }

        if (!pressed && s.last_pressed) {
            if ((now - s.press_time) >= DEBOUNCE_MS && !s.long_fired) {
                Serial.printf("[BTN%d] Short press → 0x%02X\n", i, btn_map[i].short_cmd);
                send_button_event(i, 0);
            }
        }

        s.last_pressed = pressed;
    }

    // Re-send HELLO every 30 s if not paired (until server accepts)
    if (!paired) {
        static uint32_t last_hello = 0;
        if (now - last_hello >= 30000) {
            send_hello();
            last_hello = now;
        }
    }

    delay(10);
}
