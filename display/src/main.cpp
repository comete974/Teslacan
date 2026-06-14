#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <lvgl.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "../../shared/protocol.h"
#include "config.h"

// ─── RM67162 QSPI driver (ESP-IDF SPI master) ────────────────────────────────
static spi_device_handle_t lcd_spi;

// Protocol: CMD=0x02, ADDR[23:0]={0x00, reg, 0x00}, DATA in quad mode
static void lcd_cmd(uint8_t reg) {
    spi_transaction_ext_t t = {};
    t.base.flags    = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_MODE_QIO;
    t.command_bits  = 8;
    t.address_bits  = 24;
    t.base.cmd      = 0x02;
    t.base.addr     = (uint32_t)reg << 8;
    spi_device_polling_transmit(lcd_spi, (spi_transaction_t *)&t);
}

static void lcd_data(uint8_t reg, const uint8_t *buf, size_t len) {
    spi_transaction_ext_t t = {};
    t.base.flags    = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_MODE_QIO;
    t.command_bits  = 8;
    t.address_bits  = 24;
    t.base.cmd      = 0x02;
    t.base.addr     = (uint32_t)reg << 8;
    t.base.tx_buffer = buf;
    t.base.length   = len * 8;
    spi_device_polling_transmit(lcd_spi, (spi_transaction_t *)&t);
}

static void lcd_init() {
    // Power enable
    gpio_set_direction((gpio_num_t)PIN_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_POWER, 1);
    delay(20);

    // SPI2 bus — quad mode
    spi_bus_config_t buscfg = {};
    buscfg.data0_io_num   = LCD_D0;
    buscfg.data1_io_num   = LCD_D1;
    buscfg.data2_io_num   = LCD_D2;
    buscfg.data3_io_num   = LCD_D3;
    buscfg.sclk_io_num    = LCD_SCK;
    buscfg.max_transfer_sz = DISPLAY_W * 40 * 2 + 16;
    buscfg.flags          = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {};
    devcfg.command_bits   = 8;
    devcfg.address_bits   = 24;
    devcfg.mode           = 0;
    devcfg.clock_speed_hz = 80 * 1000 * 1000;
    devcfg.spics_io_num   = LCD_CS;
    devcfg.queue_size     = 7;
    devcfg.flags          = SPI_DEVICE_HALFDUPLEX;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &lcd_spi));

    // Hardware reset
    gpio_set_direction((gpio_num_t)LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)LCD_RST, 0);
    delay(15);
    gpio_set_level((gpio_num_t)LCD_RST, 1);
    delay(120);

    // Minimal RM67162 init
    lcd_cmd(0x11);          // Sleep Out
    delay(120);
    uint8_t v;
    v = 0x55; lcd_data(0x3A, &v, 1);   // COLMOD: 16-bit RGB565
    v = 0x00; lcd_data(0x36, &v, 1);   // MADCTL: portrait
    lcd_cmd(0x29);          // Display On
    delay(20);

    Serial.println("[LCD] RM67162 QSPI init OK");
}

// Write a rectangle of RGB565 pixels
static void lcd_write_pixels(int x1, int y1, int x2, int y2,
                              const uint16_t *pixels, size_t px_count) {
    uint8_t col[4] = { (uint8_t)(x1>>8),(uint8_t)x1,(uint8_t)(x2>>8),(uint8_t)x2 };
    uint8_t row[4] = { (uint8_t)(y1>>8),(uint8_t)y1,(uint8_t)(y2>>8),(uint8_t)y2 };
    lcd_data(0x2A, col, 4);
    lcd_data(0x2B, row, 4);

    // Byte-swap RGB565 to big-endian — copy to heap so we don't mutate LVGL buffer
    uint16_t *swapped = (uint16_t *)heap_caps_malloc(px_count * 2, MALLOC_CAP_DMA);
    if (!swapped) return;
    for (size_t i = 0; i < px_count; i++)
        swapped[i] = (pixels[i] >> 8) | (pixels[i] << 8);

    spi_transaction_ext_t t = {};
    t.base.flags    = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_MODE_QIO;
    t.command_bits  = 8;
    t.address_bits  = 24;
    t.base.cmd      = 0x02;
    t.base.addr     = 0x002C00;   // Memory Write address
    t.base.tx_buffer = swapped;
    t.base.length   = px_count * 16;
    spi_device_polling_transmit(lcd_spi, (spi_transaction_t *)&t);
    heap_caps_free(swapped);
}

// ─── Touch FT3168/FT5x06 via I2C ─────────────────────────────────────────────
static bool touch_read_xy(uint16_t *tx, uint16_t *ty) {
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)5);
    if (Wire.available() < 5) return false;
    uint8_t td = Wire.read();
    if ((td & 0x0F) == 0) return false;
    uint8_t xh = Wire.read(), xl = Wire.read();
    uint8_t yh = Wire.read(), yl = Wire.read();
    *tx = ((xh & 0x0F) << 8) | xl;
    *ty = ((yh & 0x0F) << 8) | yl;
    return true;
}

// ─── LVGL draw buffer ─────────────────────────────────────────────────────────
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         lv_buf1[DISPLAY_W * 20];
static lv_color_t         lv_buf2[DISPLAY_W * 20];

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    lcd_write_pixels(area->x1, area->y1, area->x2, area->y2,
                     (uint16_t *)px, w * h);
    lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t tx, ty;
    if (touch_read_xy(&tx, &ty)) {
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

static void build_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    meter = lv_meter_create(scr);
    lv_obj_set_size(meter, 320, 320);
    lv_obj_align(meter, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(meter, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(meter, 0, 0);

    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_range(meter, scale, 0, 240, 300, 120);
    lv_meter_set_scale_ticks(meter, scale, 49, 2, 10,
                             lv_palette_lighten(LV_PALETTE_GREY, 2));
    lv_meter_set_scale_major_ticks(meter, scale, 8, 4, 15, lv_color_white(), 12);

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

    needle = lv_meter_add_needle_line(meter, scale, 4,
                                      lv_palette_main(LV_PALETTE_RED), -10);

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

    lbl_limit = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_limit, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_limit, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(lbl_limit, LV_ALIGN_TOP_RIGHT, -10, 18);
    lv_label_set_text(lbl_limit, "--");

    lbl_gear = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_gear, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_gear, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_align(lbl_gear, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    lv_label_set_text(lbl_gear, "P");

    lbl_mute = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_mute, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_mute, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_align(lbl_mute, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_label_set_text(lbl_mute, "");

    lbl_soc = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_soc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_soc, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(lbl_soc, LV_ALIGN_BOTTOM_RIGHT, -16, -30);
    lv_label_set_text(lbl_soc, "--%");

    lbl_temp = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(lbl_temp, LV_ALIGN_BOTTOM_RIGHT, -16, -10);
    lv_label_set_text(lbl_temp, "--\xC2\xB0""C");
}

// ─── UI refresh ───────────────────────────────────────────────────────────────
static const char *gear_str(uint8_t g) {
    switch (g) {
        case 1: return "R";
        case 2: return "N";
        case 3: return "D";
        default: return "P";
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

    lv_color_t spd_color = (t.speed_limit_kmh > 0 && t.speed_kmh > t.speed_limit_kmh + 3.0f)
                           ? lv_palette_main(LV_PALETTE_RED) : lv_color_white();
    lv_obj_set_style_text_color(lbl_speed, spd_color, 0);
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
    delay(300);
    Serial.println("=== TeslaCAN Display ===");

    // LCD init (QSPI)
    lcd_init();

    // Touch I2C
    Wire.begin(TOUCH_SDA, TOUCH_SCL);

    // LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, lv_buf1, lv_buf2, DISPLAY_W * 20);

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
    lv_timer_handler();

    if (data_ready) {
        data_ready = false;
        refresh_ui(latest);
    }

    delay(5);
}
