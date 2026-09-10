#include "lvgl_ui.h"

void LVGL_Ui::setup() {
    // Init Display
    if (!_gfx.begin()) {
        Serial.println("gfx->begin() failed!");
    }
    _gfx.fillScreen(RGB565_BLACK);

    pinMode(DISPLAY_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(DISPLAY_BACKLIGHT_PIN, HIGH);

    _gfx.setTextColor(RGB565_RED);
    _gfx.printCenterText("Swing Metro v1.0", DISPLAY_HEIGHT / 2, DISPLAY_WIDTH / 2);

    lv_init();
    lv_tick_set_cb([] { return millis(); });

    _display = lv_display_create(_gfx.width(), _gfx.height());
    lv_display_set_user_data(_display, this);
    lv_display_set_flush_cb(_display, flush);
    lv_display_set_buffers(_display, _drawBuffer.data(), nullptr,
                           _drawBuffer.size() * sizeof(_drawBuffer[0]),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    delay(500); // 5 seconds

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFF000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_refr_now(_display);

    delay(200);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFF0000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_refr_now(_display);

    delay(200);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFF00FF00), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_refr_now(_display);

    delay(200);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFF0000FF), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_refr_now(_display);

    delay(200);

    initMainScreen();
    lv_screen_load(_mainScreen);
}

void LVGL_Ui::loop() { lv_timer_handler(); }

void LVGL_Ui::flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    auto& self = *static_cast<LVGL_Ui*>(lv_display_get_user_data(display));
    self._gfx.draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(pixels),
                                 lv_area_get_width(area), lv_area_get_height(area));
    lv_display_flush_ready(display);
}

void LVGL_Ui::initMainScreen() {
    _mainScreen = lv_obj_create(nullptr);

    lv_obj_set_style_bg_color(_mainScreen, lv_color_black(), 0);

    auto* paramLabelsGroup = lv_obj_create(_mainScreen);
    lv_obj_set_style_align(paramLabelsGroup, LV_ALIGN_TOP_MID, 0);

    auto* tempoLabel = lv_label_create(paramLabelsGroup);
    lv_obj_set_style_text_color(tempoLabel, lv_color_hex(0xFFFF0000), 0);

    auto* swingLabel = lv_label_create(paramLabelsGroup);
    lv_obj_set_style_text_color(swingLabel, lv_color_hex(0xFF00FF00), 0);

    auto* volumeLabel = lv_label_create(paramLabelsGroup);
    lv_obj_set_style_text_color(volumeLabel, lv_color_hex(0xFF0000FF), 0);
}
