#include "step_settings_screen.h"

#include "step_grid.h"
#include "ui_theme.h"

void StepSettingsScreen::create() {
    constexpr int16_t velocityLabelY = 80;
    constexpr int16_t gateLabelY = 110;
    _root = lv_obj_create(nullptr);
    UiTheme::setScreenStyle(_root);

    _selectedStepLabel = lv_label_create(_root);
    lv_obj_set_pos(_selectedStepLabel, 10, 20);
    lv_obj_set_style_text_color(_selectedStepLabel, lv_color_hex(UiTheme::ORANGE), 0);
    lv_label_set_text(_selectedStepLabel, "Step 1");

    _selectedNoteLabel = lv_label_create(_root);
    lv_obj_set_pos(_selectedNoteLabel, 10, 50);
    lv_obj_set_style_text_color(_selectedNoteLabel, lv_color_hex(UiTheme::WHITE), 0);
    lv_label_set_text(_selectedNoteLabel, "Note: C0");

    _selectedVelocityLabel = lv_label_create(_root);
    lv_obj_set_pos(_selectedVelocityLabel, 10, velocityLabelY);
    lv_obj_set_style_text_color(_selectedVelocityLabel, lv_color_hex(UiTheme::WHITE), 0);
    lv_label_set_text(_selectedVelocityLabel, "Velocity: 127");

    _selectedGateLabel = lv_label_create(_root);
    lv_obj_set_pos(_selectedGateLabel, 10, gateLabelY);
    lv_obj_set_style_text_color(_selectedGateLabel, lv_color_hex(UiTheme::WHITE), 0);
    lv_label_set_text(_selectedGateLabel, "Gate: 100%");
}

void StepSettingsScreen::apply(const UiSettings::Editor& settings) {
    if (settings.selectedStep >= SEQUENCER_STEPS_COUNT) {
        return;
    }

    static constexpr const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                "F#", "G",  "G#", "A",  "A#", "B"};
    if (settings.selectedStep != _displayedStep) {
        _displayedStep = settings.selectedStep;
        lv_label_set_text_fmt(_selectedStepLabel, "Step %u",
                              static_cast<unsigned>(settings.selectedStep + 1));
    }
    if (settings.selectedNote != _displayedNote && settings.selectedNote >= 36) {
        _displayedNote = settings.selectedNote;
        const auto relativeNote = static_cast<uint8_t>(settings.selectedNote - 36);
        lv_label_set_text_fmt(_selectedNoteLabel, "Note: %s%u", noteNames[relativeNote % 12],
                              static_cast<unsigned>(relativeNote / 12));
    }
    if (settings.selectedVelocity != _displayedVelocity) {
        _displayedVelocity = settings.selectedVelocity;
        lv_label_set_text_fmt(_selectedVelocityLabel, "Velocity: %u",
                              static_cast<unsigned>(settings.selectedVelocity));
    }
    if (settings.selectedGate != _displayedGate) {
        _displayedGate = settings.selectedGate;
        lv_label_set_text_fmt(_selectedGateLabel, "Gate: %u%%",
                              static_cast<unsigned>(settings.selectedGate));
    }
}
