#pragma once
#include <stdint.h>

// ─── Tesla Model 3 CAN IDs (500 kbps, OBD2 port = CH bus) ───────────────────
// Source: opendbc / tesla_model3.dbc (community research)
// IMPORTANT: Verify with your car's firmware version before relying on these.

// Vehicle speed — 0x257 (ESP_B_status)
//   bits [0:12] little-endian unsigned, scale 0.01 m/s
//   km/h = raw * 0.01 * 3.6 = raw * 0.036
#define CAN_ID_VEHICLE_SPEED    0x257

// Drive/park gear — 0x118 (DI_state)
//   bits [3:6] unsigned  0=P 1=R 2=N 3=D
#define CAN_ID_DI_STATE         0x118

// Speed limit sign from navigation — 0x3D8
//   byte[3] = limit in km/h (0 = not available)
//   NOTE: ID may differ across FW versions — log CAN traffic to confirm
#define CAN_ID_SPEED_LIMIT      0x3D8

// Battery / charging info — 0x132
//   bits [0:9]  SOC × 0.1 %
#define CAN_ID_BATTERY          0x132

// Outside temperature — 0x241
//   byte[0] raw: temp = (raw - 40)  °C
#define CAN_ID_OUTSIDE_TEMP     0x241

// Trunk / frunk latch commands — transmitted TO the car
//   Confirm exact payload with your FW via candump
#define CAN_ID_BODY_CTRL        0x3A1

// ─── Speed-limit warning disable frame ───────────────────────────────────────
// TODO: These bytes are PLACEHOLDER — capture your CAN traffic to find the
// exact frame that controls the speed-limit chime on your FW version.
// Tools: candump (socketcan), savvycan, or serial log from this firmware.
//
// Approach: with car parked + speed limit warning active, record all 0x2xx-0x4xx
// frames; then re-play suspects one by one with send_can_frame().

#define SPEED_BEEP_CAN_ID       0x2B9

// Bytes to MUTE the speed limit chime — PLACEHOLDER
#define SPEED_BEEP_MUTE_FRAME   { 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

// Bytes to RESTORE the speed limit chime — PLACEHOLDER
#define SPEED_BEEP_RESTORE_FRAME { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
