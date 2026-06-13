#pragma once
#include <Arduino.h>

extern String ble_scan_json;

void ble_init();
void ble_notify_telemetry(float speed, float limit, uint8_t gear,
                          int8_t soc, int8_t temp, bool muted);
void ble_start_scan();
