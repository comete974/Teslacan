#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEScan.h>
#include "ble.h"

// ─── UUIDs ────────────────────────────────────────────────────────────────────
#define BLE_SVC_UUID   "12345678-1234-1234-1234-123456789abc"
#define BLE_TEL_UUID   "12345678-1234-1234-1234-123456789ab1"  // notify: telemetry
#define BLE_CMD_UUID   "12345678-1234-1234-1234-123456789ab2"  // write: command

static BLEServer          *ble_server    = nullptr;
static BLECharacteristic  *ble_tel_char  = nullptr;
static BLECharacteristic  *ble_cmd_char  = nullptr;
static BLEScan            *ble_scan_obj  = nullptr;
static bool                ble_connected = false;

String ble_scan_json = "[]";

// ─── Connection callbacks ─────────────────────────────────────────────────────
class ConnCB : public BLEServerCallbacks {
    void onConnect(BLEServer *s) override {
        ble_connected = true;
        Serial.println("[BLE] Client connected");
    }
    void onDisconnect(BLEServer *s) override {
        ble_connected = false;
        s->startAdvertising();
        Serial.println("[BLE] Client disconnected — re-advertising");
    }
};

// ─── Command write callback ───────────────────────────────────────────────────
// Forward declaration of command handler in main.cpp
extern void handle_command(uint8_t cmd_type, uint8_t param);

class CmdCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *ch) override {
        auto val = ch->getValue();
        if (val.length() >= 1) {
            uint8_t cmd   = val[0];
            uint8_t param = val.length() >= 2 ? val[1] : 0;
            Serial.printf("[BLE] Command 0x%02X param=%d\n", cmd, param);
            handle_command(cmd, param);
        }
    }
};

// ─── Scan callback ────────────────────────────────────────────────────────────
class ScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        // Collect results — appended after scan finishes
    }
};

// ─── Init ─────────────────────────────────────────────────────────────────────
void ble_init() {
    BLEDevice::init("TeslaCAN");

    ble_server = BLEDevice::createServer();
    ble_server->setCallbacks(new ConnCB());

    BLEService *svc = ble_server->createService(BLE_SVC_UUID);

    // Telemetry: notify characteristic
    ble_tel_char = svc->createCharacteristic(BLE_TEL_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    ble_tel_char->addDescriptor(new BLE2902());

    // Command: write characteristic
    ble_cmd_char = svc->createCharacteristic(BLE_CMD_UUID,
        BLECharacteristic::PROPERTY_WRITE);
    ble_cmd_char->setCallbacks(new CmdCB());

    svc->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SVC_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    BLEDevice::startAdvertising();

    ble_scan_obj = BLEDevice::getScan();
    ble_scan_obj->setActiveScan(true);
    ble_scan_obj->setInterval(100);
    ble_scan_obj->setWindow(99);

    Serial.println("[BLE] Advertising as 'TeslaCAN'");
}

// ─── Notify telemetry to connected phone ─────────────────────────────────────
void ble_notify_telemetry(float speed, float limit, uint8_t gear,
                          int8_t soc, int8_t temp, bool muted) {
    if (!ble_connected || !ble_tel_char) return;
    // Simple binary layout: [speed_int16_x10][limit_u8][gear][soc][temp][muted]
    uint8_t buf[8];
    int16_t spd10 = (int16_t)(speed * 10);
    buf[0] = spd10 >> 8;
    buf[1] = spd10 & 0xFF;
    buf[2] = (uint8_t)limit;
    buf[3] = gear;
    buf[4] = (uint8_t)soc;
    buf[5] = (uint8_t)(temp + 40);
    buf[6] = muted ? 1 : 0;
    buf[7] = 0;
    ble_tel_char->setValue(buf, sizeof(buf));
    ble_tel_char->notify();
}

// ─── Scan for nearby BLE devices ─────────────────────────────────────────────
void ble_start_scan() {
    Serial.println("[BLE] Starting 5s scan...");
    BLEScanResults results = ble_scan_obj->start(5, false);
    String json = "[";
    for (int i = 0; i < results.getCount(); i++) {
        BLEAdvertisedDevice dev = results.getDevice(i);
        if (i > 0) json += ",";
        json += "{\"name\":\"";
        json += dev.haveName() ? dev.getName().c_str() : "";
        json += "\",\"addr\":\"";
        json += dev.getAddress().toString().c_str();
        json += "\",\"rssi\":";
        json += dev.getRSSI();
        json += "}";
    }
    json += "]";
    ble_scan_json = json;
    ble_scan_obj->clearResults();
    Serial.printf("[BLE] Scan done: %d device(s)\n", results.getCount());
}
