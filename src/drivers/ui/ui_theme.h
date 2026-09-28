#pragma once

#include <cstdint>

#include <lvgl.h>

namespace UiTheme {

constexpr uint32_t BLACK = 0x000000;
constexpr uint32_t WHITE = 0xFFFFFF;
constexpr uint32_t RED = 0xFF0000;
constexpr uint32_t GREEN = 0x00FF00;
constexpr uint32_t BLUE = 0x0000FF;
constexpr uint32_t CYAN = 0x00FFFF;
constexpr uint32_t YELLOW = 0xFFFF00;
constexpr uint32_t ORANGE = 0xF88C00;
constexpr uint32_t DARK_GRAY = 0x101010;
constexpr uint32_t DARK_YELLOW = 0x404000;
constexpr uint32_t GRAY = 0x808080;

void setScreenStyle(lv_obj_t* screen);
void setMenuItemStyle(lv_obj_t* item, bool selected);

} // namespace UiTheme
