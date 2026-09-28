#include "lvgl_ui.h"

#include "engine/stage5_instrumentation.h"
#include "ui/ui_theme.h"

void LvglUi::setup() {
    _display.setup();

    _mainScreen.create();
    _stepSettingsScreen.create();
    initMidiClockModal();
    initProgramStorageModal();
    lv_screen_load(_mainScreen.root());
}

void LvglUi::loop() {
#if SWING_METRO_STAGE5_INSTRUMENTATION
    const auto startedAtUs = micros();
    lv_timer_handler();
    _runtimeTimingDiagnostics.recordLvTimerHandler(micros() - startedAtUs);
#else
    lv_timer_handler();
#endif
#if SWING_METRO_STAGE5_INSTRUMENTATION
    _runtimeTimingDiagnostics.publishRequestedSnapshot();
#endif
}

void LvglUi::readViewModel(const UiViewModel& viewModel) {
    const auto values = viewModel.read();
    _mainScreen.apply(values.main);
    if (values.page == UiPage::StepSettings) {
        _stepSettingsScreen.apply(values.editor);
    }

    if (values.page != _currentPage) {
        _currentPage = values.page;
        lv_screen_load(_currentPage == UiPage::StepSettings ? _stepSettingsScreen.root()
                                                            : _mainScreen.root());
    }

    setMidiClockModal(values.midiClock.modalOpen, values.midiClock.active,
                      values.midiClock.selection);
    setProgramStorageModal(values.storage.state, values.storage.selection, values.storage.action,
                           values.storage.slot, values.storage.resetChoice, values.storage.status);
}

void LvglUi::initMidiClockModal() {
    _midiClockModal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_midiClockModal, 112, 132);
    lv_obj_align(_midiClockModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_midiClockModal, lv_color_hex(UiTheme::DARK_GRAY), 0);
    lv_obj_set_style_bg_opa(_midiClockModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_midiClockModal, lv_color_hex(UiTheme::ORANGE), 0);
    lv_obj_set_style_border_width(_midiClockModal, 2, 0);
    lv_obj_set_style_pad_all(_midiClockModal, 6, 0);
    lv_obj_remove_flag(_midiClockModal, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(_midiClockModal);
    lv_label_set_text(title, "MIDI Clock");
    lv_obj_set_style_text_color(title, lv_color_hex(UiTheme::ORANGE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    _midiClockActiveLabel = lv_label_create(_midiClockModal);
    lv_obj_set_style_text_font(_midiClockActiveLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(_midiClockActiveLabel, LV_ALIGN_TOP_MID, 0, 18);

    static constexpr const char* modeNames[] = {"Off", "Internal", "External", "Cancel"};
    for (uint8_t index = 0; index < _midiClockModeLabels.size(); ++index) {
        auto* label = lv_label_create(_midiClockModal);
        _midiClockModeLabels[index] = label;
        lv_label_set_text(label, modeNames[index]);
        lv_obj_set_width(label, 92);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, static_cast<int16_t>(36 + index * 18));
    }
    lv_obj_add_flag(_midiClockModal, LV_OBJ_FLAG_HIDDEN);
}

void LvglUi::setMidiClockModal(bool open, SwingMetro::MidiClockMode active,
                               SwingMetro::MidiClockMenuItem selection) {
    const bool visibilityChanged = open != _midiClockModalVisible;
    if (visibilityChanged) {
        _midiClockModalVisible = open;
        if (open) {
            lv_obj_remove_flag(_midiClockModal, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_midiClockModal, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!open || (!visibilityChanged && active == _displayedMidiClockActive &&
                  selection == _displayedMidiClockSelection)) {
        return;
    }

    static constexpr const char* modeNames[] = {"Off", "Internal", "External"};
    _displayedMidiClockActive = active;
    _displayedMidiClockSelection = selection;
    lv_label_set_text_fmt(_midiClockActiveLabel, "Active: %s",
                          modeNames[static_cast<uint8_t>(active)]);
    for (uint8_t index = 0; index < _midiClockModeLabels.size(); ++index) {
        const bool selected = index == static_cast<uint8_t>(selection);
        UiTheme::setMenuItemStyle(_midiClockModeLabels[index], selected);
    }
}

void LvglUi::initProgramStorageModal() {
    _programStorageModal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_programStorageModal, 120, 118);
    lv_obj_align(_programStorageModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_programStorageModal, lv_color_hex(UiTheme::DARK_GRAY), 0);
    lv_obj_set_style_bg_opa(_programStorageModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_programStorageModal, lv_color_hex(UiTheme::CYAN), 0);
    lv_obj_set_style_border_width(_programStorageModal, 2, 0);
    lv_obj_remove_flag(_programStorageModal, LV_OBJ_FLAG_SCROLLABLE);
    _programStorageTitleLabel = lv_label_create(_programStorageModal);
    lv_obj_align(_programStorageTitleLabel, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_color(_programStorageTitleLabel, lv_color_hex(UiTheme::WHITE), 0);
    _programStorageValueLabel = lv_label_create(_programStorageModal);
    lv_obj_align(_programStorageValueLabel, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_style_text_color(_programStorageValueLabel, lv_color_hex(UiTheme::WHITE), 0);

    static constexpr const char* actionNames[] = {"Save", "Load", "Cancel", "Reset program"};
    for (uint8_t index = 0; index < _programStorageActionLabels.size(); ++index) {
        auto* label = lv_label_create(_programStorageModal);
        _programStorageActionLabels[index] = label;
        lv_label_set_text(label, actionNames[index]);
        lv_obj_set_width(label, 104);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UiTheme::WHITE), 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, static_cast<int16_t>(30 + index * 18));
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(_programStorageModal, LV_OBJ_FLAG_HIDDEN);
}

void LvglUi::setProgramStorageModal(SwingMetro::ProgramStorageModalState state,
                                    SwingMetro::ProgramStorageMenuItem selection,
                                    SwingMetro::ProgramStorageAction action, uint8_t slot,
                                    SwingMetro::ProgramResetChoice resetChoice,
                                    SwingMetro::ProgramStoreStatus status) {
    if (state == _displayedProgramStorageState && selection == _displayedProgramStorageSelection &&
        action == _displayedProgramStorageAction && slot == _displayedProgramStorageSlot &&
        resetChoice == _displayedProgramResetChoice && status == _displayedProgramStorageStatus) {
        return;
    }
    _displayedProgramStorageState = state;
    _displayedProgramStorageSelection = selection;
    _displayedProgramStorageAction = action;
    _displayedProgramStorageSlot = slot;
    _displayedProgramResetChoice = resetChoice;
    _displayedProgramStorageStatus = status;
    if (state == SwingMetro::ProgramStorageModalState::Closed) {
        lv_obj_add_flag(_programStorageModal, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(_programStorageModal, LV_OBJ_FLAG_HIDDEN);
    if (state == SwingMetro::ProgramStorageModalState::Action) {
        lv_label_set_text(_programStorageTitleLabel, "Save / Load");
        lv_obj_add_flag(_programStorageValueLabel, LV_OBJ_FLAG_HIDDEN);
        for (uint8_t index = 0; index < _programStorageActionLabels.size(); ++index) {
            const bool selected = index == static_cast<uint8_t>(selection);
            lv_obj_remove_flag(_programStorageActionLabels[index], LV_OBJ_FLAG_HIDDEN);
            UiTheme::setMenuItemStyle(_programStorageActionLabels[index], selected);
        }
    } else if (state == SwingMetro::ProgramStorageModalState::Slot) {
        lv_obj_remove_flag(_programStorageValueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_programStorageTitleLabel,
                          action == SwingMetro::ProgramStorageAction::Save ? "Save slot"
                                                                           : "Load slot");
        if (slot == SwingMetro::PROGRAM_STORAGE_CANCEL_SLOT) {
            lv_label_set_text(_programStorageValueLabel, "Cancel");
        } else {
            lv_label_set_text_fmt(_programStorageValueLabel, "Slot %u",
                                  static_cast<unsigned>(slot));
        }
    } else if (state == SwingMetro::ProgramStorageModalState::ResetConfirmation) {
        lv_obj_remove_flag(_programStorageValueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_programStorageTitleLabel, "Reset program?");
        lv_label_set_text(_programStorageValueLabel,
                          resetChoice == SwingMetro::ProgramResetChoice::No ? "No" : "Yes");
    } else if (state == SwingMetro::ProgramStorageModalState::Busy) {
        lv_obj_remove_flag(_programStorageValueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_programStorageTitleLabel, "Program Storage");
        if (selection == SwingMetro::ProgramStorageMenuItem::ResetProgram) {
            lv_label_set_text(_programStorageValueLabel, "Resetting...");
        } else {
            lv_label_set_text(_programStorageValueLabel,
                              action == SwingMetro::ProgramStorageAction::Save ? "Saving..."
                                                                               : "Loading...");
        }
    } else {
        lv_obj_remove_flag(_programStorageValueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_programStorageTitleLabel,
                          state == SwingMetro::ProgramStorageModalState::Success ? "Complete"
                                                                                 : "Error");
        lv_label_set_text_fmt(_programStorageValueLabel, "Status %u",
                              static_cast<unsigned>(status));
    }
    if (state != SwingMetro::ProgramStorageModalState::Action) {
        for (auto* label : _programStorageActionLabels) {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
