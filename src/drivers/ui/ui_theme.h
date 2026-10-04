#pragma once

#include <cstdint>

#include <lvgl.h>

namespace UiTheme {

constexpr uint32_t BLACK = 0x000000;
constexpr uint32_t WHITE = 0xFFFFFF;
constexpr uint32_t CYAN = 0x58F8F0;
constexpr uint32_t TEAL = 0x00B8B0;
constexpr uint32_t DARK_TEAL = 0x084C40;
constexpr uint32_t GRAY = 0x585C58;
constexpr uint32_t LIGHT_GRAY = 0xA0A4A0;
constexpr uint32_t YELLOW = 0xB8AC00;
constexpr uint32_t RED = 0xF80000;

void setScreenStyle(lv_obj_t* screen);
void setMenuItemStyle(lv_obj_t* item, bool selected);
[[nodiscard]] lv_obj_t* createModalPanel(int16_t width, int16_t height);
[[nodiscard]] lv_obj_t* createLabel(lv_obj_t* parent, const char* text, int16_t x, int16_t y,
                                    uint32_t color, const lv_font_t* font);

} // namespace UiTheme
