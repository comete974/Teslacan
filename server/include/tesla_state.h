#pragma once

// Shared across main.cpp, web.cpp, ble.cpp
struct TeslaState {
    float   speed_kmh       = 0;
    float   speed_limit_kmh = 0;
    uint8_t gear            = 0;    // 0=P 1=R 2=N 3=D
    bool    beep_muted      = false;
    bool    sentinel_on     = false;
    bool    auto_muted      = false;
    int8_t  soc             = 0;
    int8_t  outside_temp    = 0;
    uint8_t wiper_level     = 0;
    uint8_t inside_temp     = 0;
};

extern TeslaState tesla;
