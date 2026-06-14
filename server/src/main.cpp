#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "driver/twai.h"
#include "../../shared/protocol.h"
#include "can_ids.h"
#include "config.h"
#include "peers.h"
#include "web.h"
#include "ble.h"
#include "tesla_state.h"

TeslaState tesla;

// ─── CAN ─────────────────────────────────────────────────────────────────────
static void can_send(uint32_t id, const uint8_t *data, uint8_t len) {
    twai_message_t msg = {};
    msg.identifier       = id;
    msg.data_length_code = len;
    memcpy(msg.data, data, len);
    if (twai_transmit(&msg, pdMS_TO_TICKS(10)) != ESP_OK)
        Serial.printf("[CAN] TX failed 0x%03X\n", id);
}

static void mute_beep() {
    uint8_t f[] = SPEED_BEEP_MUTE_FRAME;
    can_send(SPEED_BEEP_CAN_ID, f, sizeof(f));
    tesla.beep_muted = true;
    Serial.println("[CAN] Beep MUTED");
}
static void restore_beep() {
    uint8_t f[] = SPEED_BEEP_RESTORE_FRAME;
    can_send(SPEED_BEEP_CAN_ID, f, sizeof(f));
    tesla.beep_muted = false;
    Serial.println("[CAN] Beep RESTORED");
}

// ─── Command executor (called from ESP-NOW and BLE) ───────────────────────────
void handle_command(uint8_t cmd_type, uint8_t /*param*/) {
    MsgType cmd = (MsgType)cmd_type;
    switch (cmd) {
        case MsgType::CMD_BEEP_TOGGLE:
            tesla.beep_muted ? restore_beep() : mute_beep(); break;
        case MsgType::CMD_SENTINEL_TOGGLE:
            tesla.sentinel_on = !tesla.sentinel_on;
            Serial.printf("[CMD] Sentinel %s\n", tesla.sentinel_on ? "ON":"OFF"); break;
        case MsgType::CMD_TRUNK_OPEN:   { uint8_t f[8]={0x01}; can_send(CAN_ID_BODY_CTRL,f,8); break; }
        case MsgType::CMD_TRUNK_CLOSE:  { uint8_t f[8]={0x02}; can_send(CAN_ID_BODY_CTRL,f,8); break; }
        case MsgType::CMD_FRUNK_TOGGLE: { uint8_t f[8]={0x04}; can_send(CAN_ID_BODY_CTRL,f,8); break; }
        case MsgType::CMD_LOCK:         { uint8_t f[8]={0x10}; can_send(CAN_ID_BODY_CTRL,f,8); break; }
        case MsgType::CMD_UNLOCK:       { uint8_t f[8]={0x20}; can_send(CAN_ID_BODY_CTRL,f,8); break; }
        case MsgType::CMD_HORN_SHORT:   { uint8_t f[8]={0x01,0x10}; can_send(CAN_ID_BODY_CTRL,f,8); break; }
        case MsgType::CMD_HAZARD_TOGGLE:{ uint8_t f[8]={0x08}; can_send(CAN_ID_BODY_CTRL,f,8); break; }
        case MsgType::CMD_WIPER_UP:     tesla.wiper_level = min((int)tesla.wiper_level+1,7); break;
        case MsgType::CMD_WIPER_DOWN:   if(tesla.wiper_level>0) tesla.wiper_level--; break;
        default: Serial.printf("[CMD] Unknown 0x%02X\n", cmd_type); break;
    }
}

// ─── CAN frame processor ─────────────────────────────────────────────────────
static void process_can(const twai_message_t &msg) {
    switch (msg.identifier) {
        case CAN_ID_VEHICLE_SPEED: {
            uint16_t raw = (uint16_t)(msg.data[0]|(msg.data[1]<<8)) & 0x1FFF;
            tesla.speed_kmh = raw * 0.036f;
            if (!tesla.auto_muted && tesla.speed_kmh >= AUTO_MUTE_SPEED_KMH) {
                mute_beep(); tesla.auto_muted = true;
            }
            if (tesla.speed_kmh < 0.5f && tesla.gear == 0) tesla.auto_muted = false;
            break;
        }
        case CAN_ID_DI_STATE:
            tesla.gear = (msg.data[0] >> 3) & 0x0F; break;
        case CAN_ID_SPEED_LIMIT: {
            uint8_t r = msg.data[3];
            if (r > 0 && r < 200) tesla.speed_limit_kmh = r;
            break;
        }
        case CAN_ID_BATTERY: {
            uint16_t r = (uint16_t)(msg.data[0]|(msg.data[1]<<8)) & 0x3FF;
            tesla.soc = (int8_t)(r * 0.1f);
            break;
        }
        case CAN_ID_OUTSIDE_TEMP:
            tesla.outside_temp = (int8_t)(msg.data[0] - 40); break;
    }
}

// ─── ESP-NOW receive ──────────────────────────────────────────────────────────
// arduino-esp32 3.x uses esp_now_recv_info_t* instead of uint8_t* mac
static void on_espnow_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    const uint8_t *mac = info->src_addr;
    if (len < 1) return;
    MsgType type = (MsgType)data[0];

    // ── Discovery: button board announces itself ──────────────────────────────
    if (type == MsgType::MSG_HELLO) {
        const HelloMsg *h = (const HelloMsg *)data;
        Serial.printf("[ESP-NOW] HELLO from %02X:%02X:%02X:%02X:%02X:%02X name='%s'\n",
                      mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], h->name);

        HelloAckMsg ack;
        ack.accepted = pairing_mode ? 1 : 0;

        if (pairing_mode) {
            int id = peers_add(mac, h->name, h->btn_count);
            ack.device_id = id;
            // Register ESP-NOW peer so we can reply
            if (!esp_now_is_peer_exist(mac)) {
                esp_now_peer_info_t peer = {};
                peer.channel = 1;
                peer.encrypt = false;
                memcpy(peer.peer_addr, mac, 6);
                esp_now_add_peer(&peer);
            }
            esp_now_send(mac, (const uint8_t *)&ack, sizeof(ack));
            delay(10);
            peers_push_config(id);
            Serial.printf("[ESP-NOW] Paired device %d\n", id);
        } else {
            // Not in pairing mode — still reply so board knows
            ack.device_id = 0xFF;
            if (!esp_now_is_peer_exist(mac)) {
                esp_now_peer_info_t peer = {};
                peer.channel = 1;
                peer.encrypt = false;
                memcpy(peer.peer_addr, mac, 6);
                esp_now_add_peer(&peer);
            }
            esp_now_send(mac, (const uint8_t *)&ack, sizeof(ack));
            Serial.println("[ESP-NOW] Not in pairing mode — HELLO ignored");
        }
        return;
    }

    // ── Raw button event from dynamic-config board ────────────────────────────
    if (type == MsgType::MSG_BUTTON_EVENT && len >= (int)sizeof(ButtonEventMsg)) {
        const ButtonEventMsg *ev = (const ButtonEventMsg *)data;
        peers_mark_seen(mac);
        PeerDevice *d = peers_get(ev->device_id);
        if (!d || ev->btn_index >= d->btn_count) return;
        uint8_t cmd = ev->press_type == 0
                      ? d->buttons[ev->btn_index].short_cmd
                      : d->buttons[ev->btn_index].long_cmd;
        handle_command(cmd, 0);
        return;
    }

    // ── Legacy fixed-config command ───────────────────────────────────────────
    if (len >= (int)sizeof(CommandMsg)) {
        const CommandMsg *c = (const CommandMsg *)data;
        peers_mark_seen(mac);
        handle_command((uint8_t)c->type, c->param);
    }
}

// ─── Telemetry broadcast ──────────────────────────────────────────────────────
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

    // Send to all registered peers (display + all button boards)
    for (int i = 0; i < MAX_DEVICES; i++) {
        PeerDevice *d = peers_get(i);
        if (d) esp_now_send(d->mac, (const uint8_t *)&tel, sizeof(tel));
    }

    ble_notify_telemetry(tesla.speed_kmh, tesla.speed_limit_kmh, tesla.gear,
                         tesla.soc, tesla.outside_temp, tesla.beep_muted);
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== TeslaCAN Server ===");

    // WiFi AP (channel 1) — must start before ESP-NOW
    WiFi.mode(WIFI_AP);
    // web_init() calls softAP internally on channel 1
    web_init();

    Serial.printf("MAC: %s\n", WiFi.softAPmacAddress().c_str());

    // ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init FAILED"); return;
    }
    esp_now_register_recv_cb(on_espnow_recv);

    // Load previously paired devices from NVS
    peers_init();

    // Also register display as a fixed peer if MAC is configured
    uint8_t display_mac[6] = DISPLAY_MAC;
    bool is_placeholder = true;
    for (int i = 0; i < 6; i++) if (display_mac[i] != 0xFF) { is_placeholder = false; break; }
    if (!is_placeholder) {
        esp_now_peer_info_t peer = {};
        peer.channel = 1; peer.encrypt = false;
        memcpy(peer.peer_addr, display_mac, 6);
        esp_now_add_peer(&peer);
        Serial.println("[ESP-NOW] Display peer registered");
    }

    // BLE
    ble_init();

    // CAN (TWAI)
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g,&t,&f)!=ESP_OK || twai_start()!=ESP_OK) {
        Serial.println("[CAN] Init FAILED"); return;
    }
    Serial.println("[CAN] Ready at 500 kbps");
    Serial.println("Server running. Connect to WiFi 'TeslaCAN_Config' / 192.168.4.1");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    // CAN receive
    twai_message_t msg;
    while (twai_receive(&msg, 0) == ESP_OK) process_can(msg);

    uint32_t now = millis();

    // Telemetry at 10 Hz
    static uint32_t last_tel = 0;
    if (now - last_tel >= TELEMETRY_INTERVAL_MS) {
        send_telemetry();
        last_tel = now;
    }

    // Web server
    web_loop();

    // Debug print every 2 s
    static uint32_t last_dbg = 0;
    if (now - last_dbg >= 2000) {
        Serial.printf("[State] %.1f km/h  lim=%.0f  gear=%d  muted=%d  SOC=%d%%\n",
                      tesla.speed_kmh, tesla.speed_limit_kmh, tesla.gear,
                      tesla.beep_muted, tesla.soc);
        last_dbg = now;
    }
}
