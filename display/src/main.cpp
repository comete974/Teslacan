#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include "../../shared/protocol.h"
#include "config.h"

// ─── LovyanGFX display class ─────────────────────────────────────────────────
class LGFX_AMOLED : public lgfx::LGFX_Device {
    lgfx::Panel_RM67162 _panel;
    lgfx::Bus_QSPI      _bus;
    lgfx::Touch_FT5x06  _touch;
public:
    LGFX_AMOLED() {
        {   // QSPI bus
            auto cfg = _bus.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.freq_write = 80000000;
            cfg.pin_sclk   = LCD_SCK;
            cfg.pin_d0     = LCD_D0;
            cfg.pin_d1     = LCD_D1;
            cfg.pin_d2     = LCD_D2;
            cfg.pin_d3     = LCD_D3;
            cfg.pin_dc     = -1;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // Panel
            auto cfg = _panel.config();
            cfg.pin_cs        = LCD_CS;
            cfg.pin_rst       = LCD_RST;
            cfg.pin_busy      = -1;
            cfg.panel_width   = DISPLAY_W;
            cfg.panel_height  = DISPLAY_H;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            cfg.readable      = false;
            _panel.config(cfg);
        }
        {   // Touch
            auto cfg = _touch.config();
            cfg.i2c_port   = 1;
            cfg.i2c_addr   = TOUCH_I2C_ADDR;
            cfg.pin_sda    = TOUCH_SDA;
            cfg.pin_scl    = TOUCH_SCL;
            cfg.pin_int    = TOUCH_INT;
            cfg.pin_rst    = TOUCH_RST;
            cfg.freq       = 400000;
            cfg.x_min = 0; cfg.x_max = DISPLAY_W;
            cfg.y_min = 0; cfg.y_max = DISPLAY_H;
            cfg.bus_shared = false;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

static LGFX_AMOLED gfx;

// ─── LVGL draw buffer ─────────────────────────────────────────────────────────
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         lv_buf1[DISPLAY_W * 20];
static lv_color_t         lv_buf2[DISPLAY_W * 20];

// ─── LVGL callbacks ───────────────────────────────────────────────────────────
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    gfx.startWrite();
    gfx.setAddrWindow(area->x1, area->y1, w, h);
    gfx.writePixels((lgfx::rgb565_t *)px, w * h, true);
    gfx.endWrite();
    lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t tx, ty;
    if (gfx.getTouch(&tx, &ty)) {
        data->point.x = tx;
        data->point.y = ty;
        data->state   = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ─── UI state (updated from ESP-NOW) ─────────────────────────────────────────
static TelemetryMsg latest = {};
static volatile bool data_ready = false;

// ─── UI elements ──────────────────────────────────────────────────────────────
static lv_obj_t *meter;
static lv_meter_indicator_t *needle;
static lv_obj_t *lbl_speed;
static lv_obj_t *lbl_unit;
static lv_obj_t *lbl_limit;
static lv_obj_t *lbl_gear;
static lv_obj_t *lbl_mute;
static lv_obj_t *lbl_soc;
static lv_obj_t *lbl_temp;
static lv_obj_t *arc_limit;

// ─── Build the gauge UI ───────────────────────────────────────────────────────
static void build_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ── Circular meter ────────────────────────────────────────────────────
    meter = lv_meter_create(scr);
    lv_obj_set_size(meter, 320, 320);
    lv_obj_align(meter, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(meter, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(meter, 0, 0);

    // Scale: 0-240 km/h, 300° arc, start at 120° (bottom-left)
    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_range(meter, scale, 0, 240, 300, 120);
    lv_meter_set_scale_ticks(meter, scale, 49, 2, 10,
                             lv_palette_lighten(LV_PALETTE_GREY, 2));
    lv_meter_set_scale_major_ticks(meter, scale, 8, 4, 15, lv_color_white(), 12);

    // Color arc — green zone 0-120, orange 120-180, red 180-240
    lv_meter_indicator_t *arc_g = lv_meter_add_arc(meter, scale, 8,
                                                    lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_meter_set_indicator_start_value(meter, arc_g, 0);
    lv_meter_set_indicator_end_value(meter, arc_g, 120);

    lv_meter_indicator_t *arc_o = lv_meter_add_arc(meter, scale, 8,
                                                    lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_meter_set_indicator_start_value(meter, arc_o, 120);
    lv_meter_set_indicator_end_value(meter, arc_o, 180);

    lv_meter_indicator_t *arc_r = lv_meter_add_arc(meter, scale, 8,
                                                    lv_palette_main(LV_PALETTE_RED), 0);
    lv_meter_set_indicator_start_value(meter, arc_r, 180);
    lv_meter_set_indicator_end_value(meter, arc_r, 240);

    // Speed limit arc (hidden by default, drawn over scale)
    arc_limit = lv_arc_create(meter);
    lv_obj_set_size(arc_limit, 280, 280);
    lv_obj_align(arc_limit, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(arc_limit, 120);
    lv_arc_set_bg_angles(arc_limit, 0, 300);
    lv_arc_set_value(arc_limit, 0);
    lv_obj_set_style_arc_color(arc_limit, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_limit, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc_limit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_style(arc_limit, NULL, LV_PART_KNOB);

    // Needle
    needle = lv_meter_add_needle_line(meter, scale, 4,
                                      lv_palette_main(LV_PALETTE_RED), -10);

    // ── Center: big speed number ──────────────────────────────────────────
    lbl_speed = lv_label_create(meter);
    lv_obj_set_style_text_font(lbl_speed, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_speed, lv_color_white(), 0);
    lv_obj_align(lbl_speed, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_text(lbl_speed, "0");

    lbl_unit = lv_label_create(meter);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_unit, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(lbl_unit, LV_ALIGN_CENTER, 0, 55);
    lv_label_set_text(lbl_unit, "km/h");

    // ── Speed limit badge (top-right inside meter) ────────────────────────
    lbl_limit = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_limit, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_limit, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(lbl_limit, LV_ALIGN_TOP_RIGHT, -10, 18);
    lv_label_set_text(lbl_limit, "--");

    // ── Status bar (bottom) ───────────────────────────────────────────────
    // Gear
    lbl_gear = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_gear, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_gear, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_align(lbl_gear, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    lv_label_set_text(lbl_gear, "P");

    // Mute indicator
    lbl_mute = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_mute, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_mute, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_align(lbl_mute, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_label_set_text(lbl_mute, "");

    // SOC
    lbl_soc = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_soc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_soc, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(lbl_soc, LV_ALIGN_BOTTOM_RIGHT, -16, -30);
    lv_label_set_text(lbl_soc, "--%");

    // Temperature
    lbl_temp = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(lbl_temp, LV_ALIGN_BOTTOM_RIGHT, -16, -10);
    lv_label_set_text(lbl_temp, "--°C");
}

// ─── UI refresh (called each loop when data is ready) ────────────────────────
static const char *gear_str(uint8_t g) {
    switch (g) {
        case 0: return "P";
        case 1: return "R";
        case 2: return "N";
        case 3: return "D";
        default: return "?";
    }
}

static void refresh_ui(const TelemetryMsg &t) {
    int spd = (int)t.speed_kmh;
    lv_meter_set_indicator_value(meter, needle, spd);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", spd);
    lv_label_set_text(lbl_speed, buf);

    lv_label_set_text(lbl_gear, gear_str(t.gear));

    if (t.speed_limit_kmh > 0) {
        snprintf(buf, sizeof(buf), "%d", (int)t.speed_limit_kmh);
        lv_label_set_text(lbl_limit, buf);
        // Set arc to show limit position
        int limit_pct = (int)(t.speed_limit_kmh * 100 / 240);
        lv_arc_set_value(arc_limit, limit_pct);
    } else {
        lv_label_set_text(lbl_limit, "--");
        lv_arc_set_value(arc_limit, 0);
    }

    lv_label_set_text(lbl_mute, t.beep_muted ? "MUTE" : "");

    snprintf(buf, sizeof(buf), "%d%%", t.soc);
    lv_label_set_text(lbl_soc, buf);

    snprintf(buf, sizeof(buf), "%d\xC2\xB0""C", t.outside_temp);  // UTF-8 degree sign
    lv_label_set_text(lbl_temp, buf);

    // Turn speed label red if over limit
    if (t.speed_limit_kmh > 0 && t.speed_kmh > t.speed_limit_kmh + 3.0f) {
        lv_obj_set_style_text_color(lbl_speed, lv_palette_main(LV_PALETTE_RED), 0);
    } else {
        lv_obj_set_style_text_color(lbl_speed, lv_color_white(), 0);
    }
}

// ─── ESP-NOW receive ──────────────────────────────────────────────────────────
static void on_espnow_recv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < 1 || (MsgType)data[0] != MsgType::TELEMETRY) return;
    if (len < (int)sizeof(TelemetryMsg)) return;
    memcpy(&latest, data, sizeof(TelemetryMsg));
    data_ready = true;
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("=== TeslaCAN Display ===");

    // Power on display
    pinMode(PIN_POWER, OUTPUT);
    digitalWrite(PIN_POWER, HIGH);
    delay(50);

    // Init display
    gfx.init();
    gfx.setRotation(0);
    gfx.setBrightness(200);
    gfx.fillScreen(TFT_BLACK);

    // Init LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, lv_buf1, lv_buf2, DISPLAY_W * 20);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = DISPLAY_W;
    disp_drv.ver_res   = DISPLAY_H;
    disp_drv.flush_cb  = disp_flush;
    disp_drv.draw_buf  = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    build_ui();

    // Init ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.printf("My MAC: %s\n", WiFi.macAddress().c_str());

    esp_now_init();
    esp_now_register_recv_cb(on_espnow_recv);

    uint8_t server_mac[6] = SERVER_MAC;
    esp_now_peer_info_t peer = {};
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    memcpy(peer.peer_addr, server_mac, 6);
    esp_now_add_peer(&peer);

    Serial.println("Display ready.");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    lv_timer_handler();

    if (data_ready) {
        data_ready = false;
        refresh_ui(latest);
    }

    delay(5);
}
