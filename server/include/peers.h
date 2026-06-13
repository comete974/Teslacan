#pragma once
#include <stdint.h>
#include "../../shared/protocol.h"

struct PeerDevice {
    bool    active    = false;
    uint8_t mac[6]   = {};
    char    name[12] = {};
    uint8_t btn_count = 0;
    bool    online    = false;
    BtnMap  buttons[MAX_BTNS_PER_DEV] = {};
};

void peers_init();
void peers_save();

// Returns assigned device_id (0-4) or -1 if full
int  peers_add(const uint8_t *mac, const char *name, uint8_t btn_count);

bool peers_remove_by_mac(const uint8_t *mac);
bool peers_remove_by_id(int id);

PeerDevice *peers_get(int id);
PeerDevice *peers_find_mac(const uint8_t *mac);
int         peers_count();

// Push current config to a paired button board via ESP-NOW
void peers_push_config(int device_id);
// Push config to all active devices
void peers_push_config_all();

// Called when a HELLO_ACK is sent — also registers ESP-NOW peer
void peers_register_espnow(int device_id);

// Mark device online/offline based on last-seen timestamp
void peers_mark_seen(const uint8_t *mac);

extern bool pairing_mode;
