#pragma once
#include <stdint.h>

#define MAX_BTNS_PER_DEV  8
#define MAX_DEVICES       5

// ─── Message types ────────────────────────────────────────────────────────────
enum class MsgType : uint8_t {
    // Server → Display / Buttons
    TELEMETRY           = 0x01,

    // Buttons → Server
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
    CMD_NONE            = 0x00,

    // Discovery / pairing
    MSG_HELLO           = 0x50,   // Button board → broadcast
    MSG_HELLO_ACK       = 0x51,   // Server → button board
    MSG_BTN_CFG         = 0x52,   // Server → button board (config push)
    MSG_BUTTON_EVENT    = 0x53,   // Button board → server (raw event)
};

// ─── Telemetry (server → display + all button boards) ─────────────────────────
#pragma pack(push, 1)
struct TelemetryMsg {
    MsgType type        = MsgType::TELEMETRY;
    float   speed_kmh;
    float   speed_limit_kmh;
    uint8_t gear;           // 0=P 1=R 2=N 3=D
    uint8_t beep_muted;
    uint8_t sentinel_on;
    int8_t  soc;
    int8_t  outside_temp;
    uint8_t wiper_level;
    uint8_t inside_temp;
};

// ─── Command (button board → server, legacy fixed-config path) ────────────────
struct CommandMsg {
    MsgType type;
    uint8_t param;
};

// ─── Raw button event (dynamic-config path) ───────────────────────────────────
struct ButtonEventMsg {
    MsgType type      = MsgType::MSG_BUTTON_EVENT;
    uint8_t device_id;   // assigned by server at pairing
    uint8_t btn_index;
    uint8_t press_type;  // 0=short 1=long
};

// ─── Discovery / pairing ──────────────────────────────────────────────────────
struct HelloMsg {
    MsgType type      = MsgType::MSG_HELLO;
    char    name[12];    // human-readable name e.g. "Boutons_1"
    uint8_t btn_count;
};

struct HelloAckMsg {
    MsgType type      = MsgType::MSG_HELLO_ACK;
    uint8_t device_id;   // 0-4, assigned by server
    uint8_t accepted;    // 1=paired, 0=server not in pairing mode
};

// ─── Button config (server → button board after pairing or on request) ────────
struct BtnMap {
    uint8_t short_cmd;   // MsgType cast to uint8
    uint8_t long_cmd;
};

struct BtnCfgMsg {
    MsgType type      = MsgType::MSG_BTN_CFG;
    uint8_t device_id;
    uint8_t btn_count;
    BtnMap  map[MAX_BTNS_PER_DEV];
};
#pragma pack(pop)
