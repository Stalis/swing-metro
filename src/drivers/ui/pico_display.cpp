#include "pico_display.h"

#include "engine/stage5_instrumentation.h"
#include "ui_theme.h"

void PicoDisplay::setup() {
    if (!_gfx.begin()) {
        Serial.println("gfx->begin() failed!");
    }
    _gfx.fillScreen(RGB565_BLACK);

    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    if (_gfx.width() != 160 || _gfx.height() != 128) {
        Serial.printf("Unexpected display size: %d x %d\n", _gfx.width(), _gfx.height());
    }

    _display = lv_display_create(_gfx.width(), _gfx.height());
    lv_display_set_user_data(_display, this);
    lv_display_set_flush_cb(_display, flush);
    lv_display_set_buffers(_display, _drawBuffer.data(), nullptr,
                           _drawBuffer.size() * sizeof(_drawBuffer[0]),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    showSplash();
}

void PicoDisplay::flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    auto& self = *static_cast<PicoDisplay*>(lv_display_get_user_data(display));
#if SWING_METRO_STAGE5_INSTRUMENTATION
    const auto startedAtUs = micros();
#endif
    self._gfx.draw16bitRGBBitmap(static_cast<int16_t>(area->x1), static_cast<int16_t>(area->y1),
                                 reinterpret_cast<uint16_t*>(pixels),
                                 static_cast<int16_t>(lv_area_get_width(area)),
                                 static_cast<int16_t>(lv_area_get_height(area)));
    lv_display_flush_ready(display);
#if SWING_METRO_STAGE5_INSTRUMENTATION
    self._runtimeTimingDiagnostics.recordDisplayFlush(micros() - startedAtUs);
#endif
}

void PicoDisplay::showSplash() {
    auto* screen = lv_screen_active();
    UiTheme::setScreenStyle(screen);

    auto* titleLabel = lv_label_create(screen);
    lv_label_set_text(titleLabel, "Swing Metro");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(UiTheme::RED), 0);
    lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, -8);

    auto* lvglLabel = lv_label_create(screen);
    lv_label_set_text_fmt(lvglLabel, "LVGL v%d.%d.%d", lv_version_major(), lv_version_minor(),
                          lv_version_patch());
    lv_obj_set_style_text_color(lvglLabel, lv_color_hex(UiTheme::GRAY), 0);
    lv_obj_align(lvglLabel, LV_ALIGN_CENTER, 0, 8);

    lv_refr_now(_display);
    delay(500);
}
