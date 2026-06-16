#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Arduino_GFX_Library.h>
#include <XPowersLib.h>
#include <lvgl.h>
#include "../../shared/protocol.h"
#include "config.h"

// ─── Display CO5300 via QSPI ─────────────────────────────────────────────────
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_CLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
static Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RST, 0 /* rotation */, 466, 466, 6, 0, 0, 0);

// ─── PMU AXP2101 ─────────────────────────────────────────────────────────────
static XPowersAXP2101 pmu;

// ─── Touch FT3168 (polled via Wire) ──────────────────────────────────────────
static bool touch_read(int32_t *tx, int32_t *ty) {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)5);
    if (Wire.available() < 5) return false;
    uint8_t td = Wire.read();
    uint8_t xh = Wire.read(), xl = Wire.read();
    uint8_t yh = Wire.read(), yl = Wire.read();
    if ((td & 0x0F) == 0) return false;
    *tx = ((xh & 0x0F) << 8) | xl;
    *ty = ((yh & 0x0F) << 8) | yl;
    return true;
}

// ─── LVGL callbacks ───────────────────────────────────────────────────────────
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         lv_buf1[DISPLAY_W * 10];
static lv_color_t         lv_buf2[DISPLAY_W * 10];

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    gfx->draw16bitRGBBitmap(area->x1, area->y1,
                             (uint16_t *)px,
                             area->x2 - area->x1 + 1,
                             area->y2 - area->y1 + 1);
    lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    int32_t tx, ty;
    if (touch_read(&tx, &ty)) {
        data->point.x = (lv_coord_t)tx;
        data->point.y = (lv_coord_t)ty;
        data->state   = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ─── Telemetry state ──────────────────────────────────────────────────────────
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

static void build_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Circular meter — 440×440 centered on 466×466 round screen
    meter = lv_meter_create(scr);
    lv_obj_set_size(meter, 440, 440);
    lv_obj_align(meter, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(meter, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(meter, 0, 0);

    // Scale 0–240 km/h, 300° arc
    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_range(meter, scale, 0, 240, 300, 120);
    lv_meter_set_scale_ticks(meter, scale, 49, 2, 10,
                             lv_palette_lighten(LV_PALETTE_GREY, 2));
    lv_meter_set_scale_major_ticks(meter, scale, 8, 4, 16, lv_color_white(), 14);

    // Color arcs: green 0–120, orange 120–180, red 180–240
    lv_meter_indicator_t *arc_g = lv_meter_add_arc(meter, scale, 10,
                                                    lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_meter_set_indicator_start_value(meter, arc_g, 0);
    lv_meter_set_indicator_end_value(meter, arc_g, 120);

    lv_meter_indicator_t *arc_o = lv_meter_add_arc(meter, scale, 10,
                                                    lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_meter_set_indicator_start_value(meter, arc_o, 120);
    lv_meter_set_indicator_end_value(meter, arc_o, 180);

    lv_meter_indicator_t *arc_r = lv_meter_add_arc(meter, scale, 10,
                                                    lv_palette_main(LV_PALETTE_RED), 0);
    lv_meter_set_indicator_start_value(meter, arc_r, 180);
    lv_meter_set_indicator_end_value(meter, arc_r, 240);

    // Speed limit arc (blue) drawn inside the scale
    arc_limit = lv_arc_create(meter);
    lv_obj_set_size(arc_limit, 390, 390);
    lv_obj_align(arc_limit, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(arc_limit, 120);
    lv_arc_set_bg_angles(arc_limit, 0, 300);
    lv_arc_set_value(arc_limit, 0);
    lv_obj_set_style_arc_color(arc_limit, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_limit, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc_limit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_style(arc_limit, NULL, LV_PART_KNOB);

    // Needle
    needle = lv_meter_add_needle_line(meter, scale, 5,
                                      lv_palette_main(LV_PALETTE_RED), -15);

    // Speed number (center)
    lbl_speed = lv_label_create(meter);
    lv_obj_set_style_text_font(lbl_speed, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_speed, lv_color_white(), 0);
    lv_obj_align(lbl_speed, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_text(lbl_speed, "0");

    lbl_unit = lv_label_create(meter);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_unit, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(lbl_unit, LV_ALIGN_CENTER, 0, 60);
    lv_label_set_text(lbl_unit, "km/h");

    // Speed limit badge (top right)
    lbl_limit = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_limit, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_limit, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(lbl_limit, LV_ALIGN_TOP_RIGHT, -30, 30);
    lv_label_set_text(lbl_limit, "--");

    // Gear (bottom left)
    lbl_gear = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_gear, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_gear, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_align(lbl_gear, LV_ALIGN_BOTTOM_LEFT, 30, -30);
    lv_label_set_text(lbl_gear, "P");

    // Mute indicator (bottom center)
    lbl_mute = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_mute, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_mute, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_align(lbl_mute, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_label_set_text(lbl_mute, "");

    // SOC (bottom right)
    lbl_soc = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_soc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_soc, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(lbl_soc, LV_ALIGN_BOTTOM_RIGHT, -30, -44);
    lv_label_set_text(lbl_soc, "--%");

    // Temp (bottom right below SOC)
    lbl_temp = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(lbl_temp, LV_ALIGN_BOTTOM_RIGHT, -30, -24);
    lv_label_set_text(lbl_temp, "--\xC2\xB0""C");
}

// ─── UI refresh ───────────────────────────────────────────────────────────────
static const char *gear_str(uint8_t g) {
    switch (g) {
        case 1:  return "R";
        case 2:  return "N";
        case 3:  return "D";
        default: return "P";
    }
}

static void refresh_ui(const TelemetryMsg &t) {
    char buf[16];

    lv_meter_set_indicator_value(meter, needle, (int)t.speed_kmh);
    snprintf(buf, sizeof(buf), "%d", (int)t.speed_kmh);
    lv_label_set_text(lbl_speed, buf);

    lv_label_set_text(lbl_gear, gear_str(t.gear));

    if (t.speed_limit_kmh > 0) {
        snprintf(buf, sizeof(buf), "%d", (int)t.speed_limit_kmh);
        lv_label_set_text(lbl_limit, buf);
        lv_arc_set_value(arc_limit, (int)(t.speed_limit_kmh * 100 / 240));
    } else {
        lv_label_set_text(lbl_limit, "--");
        lv_arc_set_value(arc_limit, 0);
    }

    lv_label_set_text(lbl_mute, t.beep_muted ? "MUTE" : "");

    snprintf(buf, sizeof(buf), "%d%%", t.soc);
    lv_label_set_text(lbl_soc, buf);

    snprintf(buf, sizeof(buf), "%d\xC2\xB0""C", t.outside_temp);
    lv_label_set_text(lbl_temp, buf);

    lv_color_t c = (t.speed_limit_kmh > 0 && t.speed_kmh > t.speed_limit_kmh + 3.0f)
                   ? lv_palette_main(LV_PALETTE_RED) : lv_color_white();
    lv_obj_set_style_text_color(lbl_speed, c, 0);
}

// ─── ESP-NOW receive ──────────────────────────────────────────────────────────
static void on_espnow_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    (void)info;
    if (len < 1 || (MsgType)data[0] != MsgType::TELEMETRY) return;
    if (len < (int)sizeof(TelemetryMsg)) return;
    memcpy(&latest, data, sizeof(TelemetryMsg));
    data_ready = true;
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("=== TeslaCAN Display ===");

    // I2C for PMU + touch
    Wire.begin(I2C_SDA, I2C_SCL);

    // Power on display via AXP2101
    pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    pmu.setALDO3Voltage(3300); pmu.enableALDO3();   // display VCC
    pmu.setBLDO1Voltage(1800); pmu.enableBLDO1();   // display VIO / backlight
    delay(150);

    // Display init
    gfx->begin();
    gfx->setBrightness(255);
    gfx->fillScreen(0x0000);

    // LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, lv_buf1, lv_buf2, DISPLAY_W * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = DISPLAY_W;
    disp_drv.ver_res  = DISPLAY_H;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_cb;
    lv_indev_drv_register(&indev_drv);

    build_ui();

    // ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
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
    lv_tick_inc(5);
    lv_timer_handler();

    if (data_ready) {
        data_ready = false;
        refresh_ui(latest);
    }

    delay(5);
}
