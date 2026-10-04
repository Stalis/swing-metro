#include "main_screen.h"

#include "input/ui_display_format.h"
#include "ui_fonts.h"
#include "ui_theme.h"

void MainScreen::create() {
    _root = lv_obj_create(nullptr);
    UiTheme::setScreenStyle(_root);

    _tempoLabel = UiTheme::createLabel(_root, "120 BPM", 3, 1, UiTheme::WHITE, UiFonts::medium());
    UiTheme::createLabel(_root, "CLK", 65, 2, UiTheme::CYAN, UiFonts::small());
    static constexpr const char* clockNames[] = {"OFF", "INT", "EXT"};
    for (std::uint8_t index = 0; index < 3; ++index) {
        _clockModes[index] = UiTheme::createLabel(_root, clockNames[index],
                                                  static_cast<std::int16_t>(85 + index * 23), 1,
                                                  UiTheme::LIGHT_GRAY, UiFonts::micro());
        lv_obj_set_size(_clockModes[index], 20, 9);
        lv_obj_set_style_text_align(_clockModes[index], LV_TEXT_ALIGN_CENTER, 0);
    }

    auto* divider = lv_obj_create(_root);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, 3, 11);
    lv_obj_set_size(divider, 154, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(UiTheme::GRAY), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    UiTheme::createLabel(_root, "SEQ 01 | MIDI CH /", 3, 15, UiTheme::CYAN, UiFonts::small());
    _midiChannelsLabel =
        UiTheme::createLabel(_root, "ALL", 78, 15, UiTheme::WHITE, UiFonts::small());
    _swingLabel = UiTheme::createLabel(_root, "SWING 50%", 0, 15, UiTheme::CYAN, UiFonts::small());
    lv_obj_align(_swingLabel, LV_ALIGN_TOP_RIGHT, -3, 15);
    _stepGrid.init(_root);
}

void MainScreen::apply(const UiSettings::Main& settings) {
    if (!_hasApplied || settings.tempo != _displayedTempo) {
        _displayedTempo = settings.tempo;
        lv_label_set_text_fmt(_tempoLabel, "%u BPM", static_cast<unsigned>(settings.tempo));
    }
    if (!_hasApplied || settings.swing != _displayedSwing) {
        _displayedSwing = settings.swing;
        lv_label_set_text_fmt(_swingLabel, "SWING %u%%", static_cast<unsigned>(settings.swing));
        lv_obj_align(_swingLabel, LV_ALIGN_TOP_RIGHT, -3, 15);
    }
    if (!_hasApplied || settings.midiChannelMask != _displayedMidiChannelMask) {
        _displayedMidiChannelMask = settings.midiChannelMask;
        char channels[8]{};
        SwingMetro::UiDisplayFormat::midiChannels(settings.midiChannelMask, channels,
                                                  sizeof(channels));
        lv_label_set_text(_midiChannelsLabel, channels);
    }
    if (!_hasApplied || settings.clockMode != _displayedClockMode) {
        _displayedClockMode = settings.clockMode;
        for (std::uint8_t index = 0; index < 3; ++index) {
            const bool selected = index == static_cast<std::uint8_t>(settings.clockMode);
            UiTheme::setMenuItemStyle(_clockModes[index], selected);
            lv_obj_set_style_text_color(_clockModes[index],
                                        lv_color_hex(selected ? UiTheme::CYAN : UiTheme::GRAY), 0);
        }
    }
    _hasApplied = true;
    _stepGrid.apply(settings);
}
