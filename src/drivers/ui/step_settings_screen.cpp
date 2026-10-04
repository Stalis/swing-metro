#include "step_settings_screen.h"

#include "step_grid.h"
#include "ui_fonts.h"
#include "ui_note_name.h"
#include "ui_theme.h"

namespace {
constexpr std::int16_t ROW_Y[] = {18, 37, 56, 75, 94};
constexpr const char* ROW_NAMES[] = {"MODE", "NOTE", "VEL", "GATE", "REPEAT COUNT"};
} // namespace

void StepSettingsScreen::create() {
    _root = UiTheme::createModalPanel(148, 112);
    _selectedStepLabel =
        UiTheme::createLabel(_root, "STEP 01", 6, 5, UiTheme::CYAN, UiFonts::small());
    for (std::uint8_t index = 0; index < 5; ++index) {
        _rows[index] = lv_obj_create(_root);
        lv_obj_remove_style_all(_rows[index]);
        lv_obj_set_pos(_rows[index], 5, ROW_Y[index]);
        lv_obj_set_size(_rows[index], 136, 14);
        lv_obj_set_style_bg_opa(_rows[index], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(_rows[index], 0, 0);
        lv_obj_remove_flag(_rows[index], LV_OBJ_FLAG_SCROLLABLE);
        _labels[index] = UiTheme::createLabel(_rows[index], ROW_NAMES[index], 3, 4, UiTheme::WHITE,
                                              UiFonts::small());
    }
    _modeValue = UiTheme::createLabel(_rows[0], "NORMAL", 0, 4, UiTheme::CYAN, UiFonts::small());
    _selectedNoteLabel =
        UiTheme::createLabel(_rows[1], "C0", 0, 4, UiTheme::CYAN, UiFonts::small());
    _selectedVelocityLabel =
        UiTheme::createLabel(_rows[2], "127", 0, 4, UiTheme::CYAN, UiFonts::small());
    _selectedGateLabel =
        UiTheme::createLabel(_rows[3], "100%", 0, 4, UiTheme::CYAN, UiFonts::small());
    _repeatValue = UiTheme::createLabel(_rows[4], "04", 0, 4, UiTheme::CYAN, UiFonts::small());
    lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
}

void StepSettingsScreen::setVisible(bool visible) {
    if (visible) {
        lv_obj_remove_flag(_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
    }
}

void StepSettingsScreen::apply(const UiSettings::Editor& settings) {
    if (settings.selectedStep >= SEQUENCER_STEPS_COUNT) {
        return;
    }
    if (settings.selectedStep == _displayedStep && settings.selectedNote == _displayedNote &&
        settings.selectedVelocity == _displayedVelocity &&
        settings.selectedGate == _displayedGate && settings.mode == _displayedMode &&
        settings.repeatCount == _displayedRepeatCount) {
        return;
    }
    _displayedStep = settings.selectedStep;
    _displayedNote = settings.selectedNote;
    _displayedVelocity = settings.selectedVelocity;
    _displayedGate = settings.selectedGate;
    _displayedMode = settings.mode;
    _displayedRepeatCount = settings.repeatCount;
    lv_label_set_text_fmt(_selectedStepLabel, "STEP %02u",
                          static_cast<unsigned>(settings.selectedStep + 1));
    static constexpr const char* MODE_NAMES[] = {"NORMAL", "LEGATO", "REPEAT"};
    const auto mode = static_cast<std::uint8_t>(settings.mode);
    lv_label_set_text(_modeValue, MODE_NAMES[mode < 3 ? mode : 0]);
    char note[8]{};
    UiNoteName::format(settings.selectedNote, note, sizeof(note));
    lv_label_set_text(_selectedNoteLabel, note);
    lv_label_set_text_fmt(_selectedVelocityLabel, "%03u",
                          static_cast<unsigned>(settings.selectedVelocity));
    lv_label_set_text_fmt(_selectedGateLabel, "%u%%", static_cast<unsigned>(settings.selectedGate));
    lv_label_set_text_fmt(_repeatValue, "%02u", static_cast<unsigned>(settings.repeatCount));

    lv_obj_t* values[] = {_modeValue, _selectedNoteLabel, _selectedVelocityLabel,
                          _selectedGateLabel, _repeatValue};
    for (std::uint8_t index = 0; index < 5; ++index) {
        const bool available = index == 0 || index == 3 ||
                               (settings.mode != UiStepMode::Legato &&
                                (index != 4 || settings.mode == UiStepMode::Repeat));
        const bool selected = settings.mode == UiStepMode::Repeat ? index == 4 : index == 0;
        lv_obj_set_style_bg_color(_rows[index], lv_color_hex(UiTheme::DARK_TEAL), 0);
        lv_obj_set_style_bg_opa(_rows[index], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(_rows[index], lv_color_hex(UiTheme::YELLOW), 0);
        lv_obj_set_style_border_width(_rows[index], selected ? 1 : 0, 0);
        const auto color = available ? UiTheme::CYAN : UiTheme::GRAY;
        lv_obj_set_style_text_color(_labels[index],
                                    lv_color_hex(available ? UiTheme::WHITE : UiTheme::GRAY), 0);
        lv_obj_set_style_text_color(values[index], lv_color_hex(color), 0);
        lv_obj_align(values[index], LV_ALIGN_RIGHT_MID, -3, 0);
    }
}
