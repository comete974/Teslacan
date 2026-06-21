#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Arduino_GFX_Library.h>
#include <XPowersLib.h>
#include <lvgl.h>
#include "../../shared/protocol.h"
#include "config.h"

LV_FONT_DECLARE(conthrax_200);

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

static void build_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ── Vitesse — seul élément affiché ────────────────────────────────────────
    lbl_speed = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_speed, &conthrax_200, 0);
    lv_obj_set_style_text_color(lbl_speed, lv_color_white(), 0);
    lv_label_set_text(lbl_speed, "0");
    lv_obj_center(lbl_speed);
}

// ─── UI refresh ───────────────────────────────────────────────────────────────
static void refresh_ui(const TelemetryMsg &t) {
    char buf[16];

    snprintf(buf, sizeof(buf), "%d", (int)t.speed_kmh);
    lv_label_set_text(lbl_speed, buf);
    lv_obj_center(lbl_speed);

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
