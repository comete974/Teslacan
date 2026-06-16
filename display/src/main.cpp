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
static lv_obj_t *lbl_speed;
static lv_obj_t *lbl_unit;
static lv_obj_t *lbl_limit;
static lv_obj_t *lbl_gear;
static lv_obj_t *lbl_mute;
static lv_obj_t *lbl_soc;
static lv_obj_t *lbl_temp;

static void build_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ── Vitesse — grand chiffre centré ───────────────────────────────────────
    lbl_speed = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_speed, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_speed, lv_color_white(), 0);
    lv_obj_align(lbl_speed, LV_ALIGN_CENTER, 0, -15);
    lv_label_set_text(lbl_speed, "0");

    lbl_unit = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_unit, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(lbl_unit, LV_ALIGN_CENTER, 0, 40);
    lv_label_set_text(lbl_unit, "km/h");

    // ── Limite de vitesse (haut centre) ──────────────────────────────────────
    lbl_limit = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_limit, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_limit, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(lbl_limit, LV_ALIGN_TOP_MID, 0, 55);
    lv_label_set_text(lbl_limit, "");

    // ── Statut bas ───────────────────────────────────────────────────────────
    lbl_gear = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_gear, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_gear, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_align(lbl_gear, LV_ALIGN_BOTTOM_LEFT, 50, -50);
    lv_label_set_text(lbl_gear, "P");

    lbl_mute = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_mute, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_mute, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_align(lbl_mute, LV_ALIGN_BOTTOM_MID, 0, -45);
    lv_label_set_text(lbl_mute, "");

    lbl_soc = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_soc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_soc, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(lbl_soc, LV_ALIGN_BOTTOM_RIGHT, -50, -60);
    lv_label_set_text(lbl_soc, "--%");

    lbl_temp = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(lbl_temp, LV_ALIGN_BOTTOM_RIGHT, -50, -38);
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

    snprintf(buf, sizeof(buf), "%d", (int)t.speed_kmh);
    lv_label_set_text(lbl_speed, buf);

    lv_label_set_text(lbl_gear, gear_str(t.gear));

    if (t.speed_limit_kmh > 0) {
        snprintf(buf, sizeof(buf), "%d", (int)t.speed_limit_kmh);
        lv_label_set_text(lbl_limit, buf);
    } else {
        lv_label_set_text(lbl_limit, "--");
    }

    lv_label_set_text(lbl_mute, t.beep_muted ? "MUTE" : "");

    snprintf(buf, sizeof(buf), "%d%%", t.soc);
    lv_label_set_text(lbl_soc, buf);

    snprintf(buf, sizeof(buf), "%d\xC2\xB0""C", t.outside_temp);
    lv_label_set_text(lbl_temp, buf);

    lv_color_t c;
    if (t.speed_limit_kmh > 0 && t.speed_kmh > t.speed_limit_kmh + 3.0f)
        c = lv_palette_main(LV_PALETTE_RED);
    else if (t.speed_limit_kmh > 0 && t.speed_kmh >= t.speed_limit_kmh - 5.0f)
        c = lv_palette_main(LV_PALETTE_ORANGE);
    else
        c = lv_color_white();
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
