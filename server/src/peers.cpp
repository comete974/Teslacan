#include <Arduino.h>
#include <esp_now.h>
#include <Preferences.h>
#include "peers.h"

static PeerDevice devices[MAX_DEVICES];
static Preferences prefs;
bool pairing_mode = false;

// ─── Persistence ─────────────────────────────────────────────────────────────
void peers_init() {
    prefs.begin("teslacan", false);
    uint8_t count = prefs.getUChar("dev_count", 0);
    for (int i = 0; i < count && i < MAX_DEVICES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "d%d_mac", i);
        if (prefs.getBytesLength(key) == 6) {
            prefs.getBytes(key, devices[i].mac, 6);
            snprintf(key, sizeof(key), "d%d_name", i);
            prefs.getString(key, devices[i].name, sizeof(devices[i].name));
            snprintf(key, sizeof(key), "d%d_btns", i);
            devices[i].btn_count = prefs.getUChar(key, 6);
            snprintf(key, sizeof(key), "d%d_cfg", i);
            prefs.getBytes(key, devices[i].buttons,
                           sizeof(BtnMap) * devices[i].btn_count);
            devices[i].active = true;
            // Re-register ESP-NOW peer
            peers_register_espnow(i);
        }
    }
    Serial.printf("[Peers] Loaded %d device(s) from NVS\n", count);
}

void peers_save() {
    int count = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i].active) continue;
        char key[16];
        snprintf(key, sizeof(key), "d%d_mac", i);
        prefs.putBytes(key, devices[i].mac, 6);
        snprintf(key, sizeof(key), "d%d_name", i);
        prefs.putString(key, devices[i].name);
        snprintf(key, sizeof(key), "d%d_btns", i);
        prefs.putUChar(key, devices[i].btn_count);
        snprintf(key, sizeof(key), "d%d_cfg", i);
        prefs.putBytes(key, devices[i].buttons,
                       sizeof(BtnMap) * devices[i].btn_count);
        count++;
    }
    prefs.putUChar("dev_count", count);
}

// ─── CRUD ─────────────────────────────────────────────────────────────────────
int peers_add(const uint8_t *mac, const char *name, uint8_t btn_count) {
    // Check if already known
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].active && memcmp(devices[i].mac, mac, 6) == 0)
            return i;  // already paired, return existing id
    }
    // Find free slot
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i].active) {
            memcpy(devices[i].mac, mac, 6);
            strncpy(devices[i].name, name, sizeof(devices[i].name) - 1);
            devices[i].btn_count = min((int)btn_count, MAX_BTNS_PER_DEV);
            devices[i].active    = true;
            devices[i].online    = true;
            // Default config: first 6 commands mapped in order
            static const MsgType defaults[][2] = {
                { MsgType::CMD_BEEP_TOGGLE,    MsgType::CMD_HAZARD_TOGGLE   },
                { MsgType::CMD_SENTINEL_TOGGLE,MsgType::CMD_SENTRY_LIGHTS   },
                { MsgType::CMD_TRUNK_OPEN,     MsgType::CMD_TRUNK_CLOSE     },
                { MsgType::CMD_FRUNK_TOGGLE,   MsgType::CMD_LOCK            },
                { MsgType::CMD_CLIMATE_ON,     MsgType::CMD_CLIMATE_OFF     },
                { MsgType::CMD_VOLUME_UP,      MsgType::CMD_WIPER_UP        },
            };
            for (int b = 0; b < devices[i].btn_count; b++) {
                if (b < 6) {
                    devices[i].buttons[b].short_cmd = (uint8_t)defaults[b][0];
                    devices[i].buttons[b].long_cmd  = (uint8_t)defaults[b][1];
                } else {
                    devices[i].buttons[b].short_cmd = (uint8_t)MsgType::CMD_NONE;
                    devices[i].buttons[b].long_cmd  = (uint8_t)MsgType::CMD_NONE;
                }
            }
            peers_register_espnow(i);
            peers_save();
            Serial.printf("[Peers] Added device %d: %s\n", i, name);
            return i;
        }
    }
    return -1;  // full
}

bool peers_remove_by_mac(const uint8_t *mac) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].active && memcmp(devices[i].mac, mac, 6) == 0) {
            esp_now_del_peer(devices[i].mac);
            memset(&devices[i], 0, sizeof(PeerDevice));
            peers_save();
            return true;
        }
    }
    return false;
}

bool peers_remove_by_id(int id) {
    if (id < 0 || id >= MAX_DEVICES || !devices[id].active) return false;
    esp_now_del_peer(devices[id].mac);
    memset(&devices[id], 0, sizeof(PeerDevice));
    peers_save();
    return true;
}

PeerDevice *peers_get(int id) {
    if (id < 0 || id >= MAX_DEVICES) return nullptr;
    return devices[id].active ? &devices[id] : nullptr;
}

PeerDevice *peers_find_mac(const uint8_t *mac) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].active && memcmp(devices[i].mac, mac, 6) == 0)
            return &devices[i];
    }
    return nullptr;
}

int peers_count() {
    int n = 0;
    for (int i = 0; i < MAX_DEVICES; i++) if (devices[i].active) n++;
    return n;
}

// ─── ESP-NOW peer registration ────────────────────────────────────────────────
void peers_register_espnow(int device_id) {
    PeerDevice *d = peers_get(device_id);
    if (!d) return;
    if (!esp_now_is_peer_exist(d->mac)) {
        esp_now_peer_info_t peer = {};
        peer.channel = 1;
        peer.encrypt = false;
        memcpy(peer.peer_addr, d->mac, 6);
        esp_now_add_peer(&peer);
    }
}

// ─── Config push ──────────────────────────────────────────────────────────────
void peers_push_config(int device_id) {
    PeerDevice *d = peers_get(device_id);
    if (!d) return;
    BtnCfgMsg cfg;
    cfg.device_id = device_id;
    cfg.btn_count = d->btn_count;
    for (int i = 0; i < d->btn_count; i++)
        cfg.map[i] = d->buttons[i];
    esp_now_send(d->mac, (const uint8_t *)&cfg, sizeof(cfg));
    Serial.printf("[Peers] Config pushed to device %d\n", device_id);
}

void peers_push_config_all() {
    for (int i = 0; i < MAX_DEVICES; i++)
        if (devices[i].active) peers_push_config(i);
}

void peers_mark_seen(const uint8_t *mac) {
    PeerDevice *d = peers_find_mac(mac);
    if (d) d->online = true;
}
