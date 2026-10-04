#include "midi_clock_dialog.h"

#include "ui_fonts.h"
#include "ui_theme.h"

void MidiClockDialog::create() {
    _modal = UiTheme::createModalPanel(148, 112);
    UiTheme::createLabel(_modal, "MIDI CLOCK", 6, 5, UiTheme::CYAN, UiFonts::small());
    UiTheme::createLabel(_modal, "ACTIVE", 6, 21, UiTheme::WHITE, UiFonts::small());
    _activeLabel = UiTheme::createLabel(_modal, "OFF", 0, 21, UiTheme::CYAN, UiFonts::small());

    auto* divider = lv_obj_create(_modal);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, 6, 37);
    lv_obj_set_size(divider, 136, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(UiTheme::GRAY), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    static constexpr const char* MODE_NAMES[] = {"OFF", "INTERNAL", "EXTERNAL", "CANCEL"};
    for (std::uint8_t index = 0; index < _modeLabels.size(); ++index) {
        _modeLabels[index] = UiTheme::createLabel(_modal, MODE_NAMES[index], 9,
                                                  static_cast<std::int16_t>(44 + index * 15),
                                                  UiTheme::WHITE, UiFonts::small());
        lv_obj_set_size(_modeLabels[index], 130, 13);
    }
    lv_obj_add_flag(_modal, LV_OBJ_FLAG_HIDDEN);
}

void MidiClockDialog::apply(const UiSettings::MidiClock& settings) {
    const bool visibilityChanged = _visible != settings.modalOpen;
    _visible = settings.modalOpen;
    if (!_visible) {
        lv_obj_add_flag(_modal, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(_modal, LV_OBJ_FLAG_HIDDEN);
    if (!visibilityChanged && settings.active == _displayedActive &&
        settings.selection == _displayedSelection) {
        return;
    }
    _displayedActive = settings.active;
    _displayedSelection = settings.selection;
    static constexpr const char* MODE_NAMES[] = {"OFF", "INTERNAL", "EXTERNAL"};
    const auto active = static_cast<std::uint8_t>(settings.active);
    lv_label_set_text(_activeLabel, MODE_NAMES[active < 3 ? active : 0]);
    lv_obj_align(_activeLabel, LV_ALIGN_TOP_RIGHT, -9, 21);
    for (std::uint8_t index = 0; index < _modeLabels.size(); ++index) {
        UiTheme::setMenuItemStyle(_modeLabels[index],
                                  index == static_cast<std::uint8_t>(settings.selection));
        lv_obj_set_style_text_color(_modeLabels[index], lv_color_hex(UiTheme::WHITE), 0);
    }
}
