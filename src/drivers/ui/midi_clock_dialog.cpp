#include "midi_clock_dialog.h"

#include "ui_theme.h"

void MidiClockDialog::create() {
    _modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_modal, 112, 132);
    lv_obj_align(_modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_modal, lv_color_hex(UiTheme::DARK_GRAY), 0);
    lv_obj_set_style_bg_opa(_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_modal, lv_color_hex(UiTheme::ORANGE), 0);
    lv_obj_set_style_border_width(_modal, 2, 0);
    lv_obj_set_style_pad_all(_modal, 6, 0);
    lv_obj_remove_flag(_modal, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(_modal);
    lv_label_set_text(title, "MIDI Clock");
    lv_obj_set_style_text_color(title, lv_color_hex(UiTheme::ORANGE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    _activeLabel = lv_label_create(_modal);
    lv_obj_set_style_text_font(_activeLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(_activeLabel, LV_ALIGN_TOP_MID, 0, 18);

    static constexpr const char* modeNames[] = {"Off", "Internal", "External", "Cancel"};
    for (uint8_t index = 0; index < _modeLabels.size(); ++index) {
        auto* label = lv_label_create(_modal);
        _modeLabels[index] = label;
        lv_label_set_text(label, modeNames[index]);
        lv_obj_set_width(label, 92);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, static_cast<int16_t>(36 + index * 18));
    }
    lv_obj_add_flag(_modal, LV_OBJ_FLAG_HIDDEN);
}

void MidiClockDialog::apply(const UiSettings::MidiClock& settings) {
    const bool visibilityChanged = settings.modalOpen != _visible;
    if (visibilityChanged) {
        _visible = settings.modalOpen;
        if (settings.modalOpen) {
            lv_obj_remove_flag(_modal, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_modal, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!settings.modalOpen || (!visibilityChanged && settings.active == _displayedActive &&
                                settings.selection == _displayedSelection)) {
        return;
    }

    static constexpr const char* modeNames[] = {"Off", "Internal", "External"};
    _displayedActive = settings.active;
    _displayedSelection = settings.selection;
    lv_label_set_text_fmt(_activeLabel, "Active: %s",
                          modeNames[static_cast<uint8_t>(settings.active)]);
    for (uint8_t index = 0; index < _modeLabels.size(); ++index) {
        const bool selected = index == static_cast<uint8_t>(settings.selection);
        UiTheme::setMenuItemStyle(_modeLabels[index], selected);
    }
}
