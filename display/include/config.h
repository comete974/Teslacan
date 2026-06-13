#pragma once

// ─── Waveshare ESP32-S3-Touch-AMOLED-1.75 (RM67162) ─────────────────────────
// QSPI display interface
#define DISPLAY_W       368
#define DISPLAY_H       448

#define LCD_CS          10
#define LCD_SCK         12
#define LCD_D0          11
#define LCD_D1          13
#define LCD_D2          14
#define LCD_D3          15
#define LCD_RST         16
#define LCD_TE          17

// Touch FT3168 (I2C)
#define TOUCH_SDA       6
#define TOUCH_SCL       7
#define TOUCH_INT       8
#define TOUCH_RST       9
#define TOUCH_I2C_ADDR  0x38

// Power / backlight enable
#define PIN_POWER       38

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
#define ESPNOW_CHANNEL  1

// Server MAC — fill after reading server Serial output
#define SERVER_MAC { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
