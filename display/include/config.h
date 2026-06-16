#pragma once

// ─── Waveshare ESP32-S3-Touch-AMOLED-1.75 (CO5300, 466×466 round) ────────────
#define DISPLAY_W   466
#define DISPLAY_H   466

// QSPI display (from working example)
#define LCD_CS      12
#define LCD_CLK     38
#define LCD_D0       4
#define LCD_D1       5
#define LCD_D2       6
#define LCD_D3       7
#define LCD_RST     39

// I2C bus shared by PMU (AXP2101) and touch (FT3168)
#define I2C_SDA     15
#define I2C_SCL     14

// Touch FT3168 — polled via Wire
#define TOUCH_ADDR  0x38

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
#define ESPNOW_CHANNEL  1

// Server MAC — fill after reading server Serial output
#define SERVER_MAC { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
