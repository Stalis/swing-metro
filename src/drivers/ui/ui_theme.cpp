#include "ui_theme.h"

#include "ui_fonts.h"

namespace UiTheme {

void setScreenStyle(lv_obj_t* screen) {
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(BLACK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(screen, UiFonts::small(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void setMenuItemStyle(lv_obj_t* item, bool selected) {
    lv_obj_set_style_bg_color(item, lv_color_hex(selected ? DARK_TEAL : BLACK), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(YELLOW), 0);
    lv_obj_set_style_border_width(item, selected ? 1 : 0, 0);
}

lv_obj_t* createModalPanel(int16_t width, int16_t height) {
    auto* panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, width, height);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(BLACK), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(CYAN), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_text_font(panel, UiFonts::small(), 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

lv_obj_t* createLabel(lv_obj_t* parent, const char* text, int16_t x, int16_t y, uint32_t color,
                      const lv_font_t* font) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font, 0);
    return label;
}

} // namespace UiTheme
