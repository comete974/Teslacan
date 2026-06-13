#pragma once
#include <stdint.h>

// ─── Message types ───────────────────────────────────────────────────────────
enum class MsgType : uint8_t {
    // Server → Display / Buttons
    TELEMETRY           = 0x01,

    // Buttons → Server (toggle/action commands)
    CMD_BEEP_TOGGLE     = 0x10,
    CMD_SENTINEL_TOGGLE = 0x11,
    CMD_TRUNK_OPEN      = 0x12,
    CMD_TRUNK_CLOSE     = 0x13,
    CMD_FRUNK_TOGGLE    = 0x14,
    CMD_LOCK            = 0x15,
    CMD_UNLOCK          = 0x16,
    CMD_CLIMATE_ON      = 0x17,
    CMD_CLIMATE_OFF     = 0x18,
    CMD_VOLUME_UP       = 0x19,
    CMD_VOLUME_DOWN     = 0x1A,
    CMD_WIPER_UP        = 0x1B,
    CMD_WIPER_DOWN      = 0x1C,
    CMD_HAZARD_TOGGLE   = 0x1D,
    CMD_DEFROST_TOGGLE  = 0x1E,
    CMD_SUNROOF_TOGGLE  = 0x1F,
    CMD_CAMP_MODE       = 0x20,
    CMD_DOG_MODE        = 0x21,
    CMD_SENTRY_LIGHTS   = 0x22,
    CMD_HORN_SHORT      = 0x23,
};

// ─── Telemetry payload (server → display + buttons) ──────────────────────────
#pragma pack(push, 1)
struct TelemetryMsg {
    MsgType type        = MsgType::TELEMETRY;
    float   speed_kmh;
    float   speed_limit_kmh;
    uint8_t gear;           // 0=P 1=R 2=N 3=D
    uint8_t beep_muted;     // 1=muted
    uint8_t sentinel_on;    // 1=active
    int8_t  soc;            // battery state of charge 0-100
    int8_t  outside_temp;   // °C
    uint8_t wiper_level;    // 0-7
    uint8_t inside_temp;    // °C
};

// ─── Command payload (buttons → server) ──────────────────────────────────────
struct CommandMsg {
    MsgType type;
    uint8_t param;  // optional parameter (e.g. wiper level, volume step)
};
#pragma pack(pop)

static_assert(sizeof(TelemetryMsg) <= 250, "ESP-NOW max payload exceeded");
static_assert(sizeof(CommandMsg)   <= 250, "ESP-NOW max payload exceeded");
