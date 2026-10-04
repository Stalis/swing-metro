#pragma once

#include <lvgl.h>

namespace UiFonts {

void initialize();
[[nodiscard]] const lv_font_t* micro();
[[nodiscard]] const lv_font_t* small();
[[nodiscard]] const lv_font_t* medium();
[[nodiscard]] const lv_font_t* large();

} // namespace UiFonts
