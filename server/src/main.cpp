#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "driver/twai.h"
#include "../../shared/protocol.h"
#include "can_ids.h"
#include "config.h"

// ─── Global state ─────────────────────────────────────────────────────────────
struct TeslaState {
    float   speed_kmh       = 0;
    float   speed_limit_kmh = 0;
    uint8_t gear            = 0;    // 0=P 1=R 2=N 3=D
    bool    beep_muted      = false;
    bool    sentinel_on     = false;
    bool    auto_muted      = false; // tracks if auto-mute was already applied
    int8_t  soc             = 0;
    int8_t  outside_temp    = 0;
    uint8_t wiper_level     = 0;
    uint8_t inside_temp     = 0;
};

static TeslaState tesla;

// ─── ESP-NOW peers ────────────────────────────────────────────────────────────
static uint8_t display_mac[6] = DISPLAY_MAC;
static uint8_t buttons_mac[6] = BUTTONS_MAC;

// ─── CAN helpers ─────────────────────────────────────────────────────────────
static void can_send(uint32_t id, const uint8_t *data, uint8_t len) {
    twai_message_t msg = {};
    msg.identifier = id;
    msg.data_length_code = len;
    memcpy(msg.data, data, len);
    if (twai_transmit(&msg, pdMS_TO_TICKS(10)) != ESP_OK) {
        Serial.printf("[CAN] TX failed id=0x%03X\n", id);
    }
}

static void mute_speed_beep() {
    uint8_t frame[] = SPEED_BEEP_MUTE_FRAME;
    can_send(SPEED_BEEP_CAN_ID, frame, sizeof(frame));
    tesla.beep_muted = true;
    Serial.println("[CAN] Speed beep MUTED");
}

static void restore_speed_beep() {
    uint8_t frame[] = SPEED_BEEP_RESTORE_FRAME;
    can_send(SPEED_BEEP_CAN_ID, frame, sizeof(frame));
    tesla.beep_muted = false;
    Serial.println("[CAN] Speed beep RESTORED");
}

// ─── CAN frame processor ─────────────────────────────────────────────────────
static void process_can(const twai_message_t &msg) {
    switch (msg.identifier) {

        case CAN_ID_VEHICLE_SPEED: {
            uint16_t raw = (uint16_t)(msg.data[0] | (msg.data[1] << 8)) & 0x1FFF;
            tesla.speed_kmh = raw * 0.036f;  // 0.01 m/s → km/h

            // Auto-mute on first motion after parking
            if (!tesla.auto_muted && tesla.speed_kmh >= AUTO_MUTE_SPEED_KMH) {
                mute_speed_beep();
                tesla.auto_muted = true;
            }
            // Reset auto-mute flag when car is parked again
            if (tesla.speed_kmh < 0.5f && tesla.gear == 0) {
                tesla.auto_muted = false;
            }
            break;
        }

        case CAN_ID_DI_STATE: {
            tesla.gear = (msg.data[0] >> 3) & 0x0F;
            break;
        }

        case CAN_ID_SPEED_LIMIT: {
            uint8_t raw = msg.data[3];
            if (raw > 0 && raw < 200) {
                tesla.speed_limit_kmh = raw;
            }
            break;
        }

        case CAN_ID_BATTERY: {
            uint16_t raw = (uint16_t)(msg.data[0] | (msg.data[1] << 8)) & 0x3FF;
            tesla.soc = (int8_t)(raw * 0.1f);
            break;
        }

        case CAN_ID_OUTSIDE_TEMP: {
            tesla.outside_temp = (int8_t)(msg.data[0] - 40);
            break;
        }
    }
}

// ─── ESP-NOW send telemetry ───────────────────────────────────────────────────
static void send_telemetry() {
    TelemetryMsg tel;
    tel.speed_kmh       = tesla.speed_kmh;
    tel.speed_limit_kmh = tesla.speed_limit_kmh;
    tel.gear            = tesla.gear;
    tel.beep_muted      = tesla.beep_muted ? 1 : 0;
    tel.sentinel_on     = tesla.sentinel_on ? 1 : 0;
    tel.soc             = tesla.soc;
    tel.outside_temp    = tesla.outside_temp;
    tel.wiper_level     = tesla.wiper_level;
    tel.inside_temp     = tesla.inside_temp;

    esp_now_send(display_mac, (const uint8_t *)&tel, sizeof(tel));
    esp_now_send(buttons_mac, (const uint8_t *)&tel, sizeof(tel));
}

// ─── ESP-NOW receive (commands from buttons) ──────────────────────────────────
static void on_espnow_recv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < 1) return;
    CommandMsg cmd;
    memcpy(&cmd, data, min(len, (int)sizeof(cmd)));

    switch (cmd.type) {

        case MsgType::CMD_BEEP_TOGGLE:
            tesla.beep_muted ? restore_speed_beep() : mute_speed_beep();
            break;

        case MsgType::CMD_SENTINEL_TOGGLE:
            tesla.sentinel_on = !tesla.sentinel_on;
            // TODO: add CAN frame for sentinel once ID confirmed
            Serial.printf("[CMD] Sentinel %s\n", tesla.sentinel_on ? "ON" : "OFF");
            break;

        case MsgType::CMD_TRUNK_OPEN: {
            // TODO: replace with confirmed CAN frame for trunk latch
            uint8_t frame[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            can_send(CAN_ID_BODY_CTRL, frame, 8);
            break;
        }

        case MsgType::CMD_TRUNK_CLOSE: {
            uint8_t frame[8] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            can_send(CAN_ID_BODY_CTRL, frame, 8);
            break;
        }

        case MsgType::CMD_FRUNK_TOGGLE: {
            uint8_t frame[8] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            can_send(CAN_ID_BODY_CTRL, frame, 8);
            break;
        }

        case MsgType::CMD_LOCK: {
            uint8_t frame[8] = {0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            can_send(CAN_ID_BODY_CTRL, frame, 8);
            break;
        }

        case MsgType::CMD_UNLOCK: {
            uint8_t frame[8] = {0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            can_send(CAN_ID_BODY_CTRL, frame, 8);
            break;
        }

        case MsgType::CMD_HORN_SHORT: {
            // Short horn pulse
            uint8_t frame[8] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            can_send(CAN_ID_BODY_CTRL, frame, 8);
            break;
        }

        case MsgType::CMD_HAZARD_TOGGLE: {
            uint8_t frame[8] = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            can_send(CAN_ID_BODY_CTRL, frame, 8);
            break;
        }

        case MsgType::CMD_WIPER_UP:
            tesla.wiper_level = min((int)tesla.wiper_level + 1, 7);
            Serial.printf("[CMD] Wiper level %d\n", tesla.wiper_level);
            break;

        case MsgType::CMD_WIPER_DOWN:
            if (tesla.wiper_level > 0) tesla.wiper_level--;
            Serial.printf("[CMD] Wiper level %d\n", tesla.wiper_level);
            break;

        default:
            Serial.printf("[CMD] Unknown type 0x%02X\n", (uint8_t)cmd.type);
            break;
    }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== TeslaCAN Server ===");

    // Init ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.printf("My MAC: %s\n", WiFi.macAddress().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init FAILED");
        return;
    }
    esp_now_register_recv_cb(on_espnow_recv);

    auto add_peer = [](const uint8_t *mac, const char *name) {
        esp_now_peer_info_t peer = {};
        peer.channel = ESPNOW_CHANNEL;
        peer.encrypt = false;
        memcpy(peer.peer_addr, mac, 6);
        esp_err_t err = esp_now_add_peer(&peer);
        Serial.printf("[ESP-NOW] Peer %-8s %s\n", name,
                      err == ESP_OK ? "OK" : "BROADCAST");
    };
    add_peer(display_mac, "display");
    add_peer(buttons_mac, "buttons");

    // Init TWAI (CAN)
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g, &t, &f) != ESP_OK ||
        twai_start() != ESP_OK) {
        Serial.println("[CAN] Init FAILED");
        return;
    }
    Serial.println("[CAN] Ready at 500 kbps");
    Serial.println("Server running.");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    // Drain incoming CAN frames (non-blocking)
    twai_message_t msg;
    while (twai_receive(&msg, 0) == ESP_OK) {
        process_can(msg);
    }

    // Periodic telemetry broadcast
    static uint32_t last_tel = 0;
    uint32_t now = millis();
    if (now - last_tel >= TELEMETRY_INTERVAL_MS) {
        send_telemetry();
        last_tel = now;
    }

    // Periodic debug print
    static uint32_t last_dbg = 0;
    if (now - last_dbg >= 2000) {
        Serial.printf("[State] %.1f km/h  limit=%.0f  gear=%d  muted=%d  SOC=%d%%  %d°C\n",
                      tesla.speed_kmh, tesla.speed_limit_kmh, tesla.gear,
                      tesla.beep_muted, tesla.soc, tesla.outside_temp);
        last_dbg = now;
    }
}
