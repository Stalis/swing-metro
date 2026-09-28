#include "ui_theme.h"

namespace UiTheme {

void setScreenStyle(lv_obj_t* screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(BLACK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

void setMenuItemStyle(lv_obj_t* item, bool selected) {
    lv_obj_set_style_text_color(item, selected ? lv_color_hex(YELLOW) : lv_color_hex(WHITE), 0);
    lv_obj_set_style_bg_color(item, selected ? lv_color_hex(DARK_YELLOW) : lv_color_hex(BLACK), 0);
    lv_obj_set_style_bg_opa(item, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

} // namespace UiTheme
