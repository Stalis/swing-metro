#include "lvgl_ui.h"

void LvglUi::setup() {
    // Init Display
    if (!_gfx.begin()) {
        Serial.println("gfx->begin() failed!");
    }
    _gfx.fillScreen(RGB565_BLACK);

    pinMode(DISPLAY_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(DISPLAY_BACKLIGHT_PIN, HIGH);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    _display = lv_display_create(_gfx.width(), _gfx.height());
    lv_display_set_user_data(_display, this);
    lv_display_set_flush_cb(_display, flush);
    lv_display_set_buffers(_display, _drawBuffer.data(), nullptr,
                           _drawBuffer.size() * sizeof(_drawBuffer[0]),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    auto* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto* titleLabel = lv_label_create(screen);
    lv_label_set_text(titleLabel, "Swing Metro");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFF0000), 0);
    lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, -8);

    auto* lvglLabel = lv_label_create(screen);
    lv_label_set_text_fmt(lvglLabel, "LVGL v%d.%d.%d", lv_version_major(), lv_version_minor(),
                          lv_version_patch());
    lv_obj_set_style_text_color(lvglLabel, lv_color_hex(0x808080), 0);
    lv_obj_align(lvglLabel, LV_ALIGN_CENTER, 0, 8);

    lv_refr_now(_display);
    delay(500); // 5 seconds

    lv_subject_init_int(&_tempoSubject, 120);
    lv_subject_set_max_value_int(&_tempoSubject, 240);
    lv_subject_set_min_value_int(&_tempoSubject, 40);

    lv_subject_init_int(&_swingSubject, 50);
    lv_subject_set_min_value_int(&_swingSubject, 50);
    lv_subject_set_max_value_int(&_swingSubject, 100);

    lv_subject_init_int(&_volumeSubject, 100);
    lv_subject_set_min_value_int(&_volumeSubject, 0);
    lv_subject_set_max_value_int(&_volumeSubject, 100);

    lv_subject_init_int(&_sequencerStepsSubject, 0);

    initMainScreen();
    initStepSettingsScreen();
    initMidiClockModal();
    initProgramStorageModal();
    lv_screen_load(_mainScreen);
}

void LvglUi::loop() { lv_timer_handler(); }

void LvglUi::readViewModel(const UiViewModel& viewModel) {
    const auto values = viewModel.read();
    setTempo(values.tempo);
    setSwing(values.swing);
    setVolume(values.volume);

    setSteps(values.notesState, values.activeNote);

    if (values.page == UiPage::StepSettings && values.selectedStep < SEQUENCER_STEPS_COUNT) {
        static constexpr const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                    "F#", "G",  "G#", "A",  "A#", "B"};
        if (values.selectedStep != _displayedStep) {
            _displayedStep = values.selectedStep;
            lv_label_set_text_fmt(_selectedStepLabel, "Step %u",
                                  static_cast<unsigned>(values.selectedStep + 1));
        }
        if (values.selectedNote != _displayedNote && values.selectedNote >= 36) {
            _displayedNote = values.selectedNote;
            const auto relativeNote = static_cast<uint8_t>(values.selectedNote - 36);
            lv_label_set_text_fmt(_selectedNoteLabel, "Note: %s%u", noteNames[relativeNote % 12],
                                  static_cast<unsigned>(relativeNote / 12));
        }
        if (values.selectedVelocity != _displayedVelocity) {
            _displayedVelocity = values.selectedVelocity;
            lv_label_set_text_fmt(_selectedVelocityLabel, "Velocity: %u",
                                  static_cast<unsigned>(values.selectedVelocity));
        }
        if (values.selectedGate != _displayedGate) {
            _displayedGate = values.selectedGate;
            lv_label_set_text_fmt(_selectedGateLabel, "Gate: %u%%",
                                  static_cast<unsigned>(values.selectedGate));
        }
    }

    if (values.page != _currentPage) {
        _currentPage = values.page;
        lv_screen_load(_currentPage == UiPage::StepSettings ? _stepSettingsScreen : _mainScreen);
    }

    setMidiClockModal(values.midiClockModalOpen, values.midiClockActive, values.midiClockPreview);
    setExternalClock(values.externalClockStatus, values.externalTempo);
    setProgramStorageModal(values.programStorageState, values.programStorageAction,
                           values.programStorageSlot, values.programStorageStatus);
}

void LvglUi::setTempo(uint8_t value) { lv_subject_set_int(&_tempoSubject, value); }
void LvglUi::setSwing(uint8_t value) { lv_subject_set_int(&_swingSubject, value); }
void LvglUi::setVolume(uint8_t value) { lv_subject_set_int(&_volumeSubject, value); }

void LvglUi::setSteps(std::bitset<SEQUENCER_STEPS_COUNT> stepsState, uint8_t activeStep) {
    lv_subject_set_int(&_sequencerStepsSubject,
                       stepsState.to_ulong() | (static_cast<uint16_t>(activeStep) << 16));
}

void LvglUi::flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    auto& self = *static_cast<LvglUi*>(lv_display_get_user_data(display));
    self._gfx.draw16bitRGBBitmap(static_cast<int16_t>(area->x1), static_cast<int16_t>(area->y1),
                                 reinterpret_cast<uint16_t*>(pixels),
                                 static_cast<int16_t>(lv_area_get_width(area)),
                                 static_cast<int16_t>(lv_area_get_height(area)));
    lv_display_flush_ready(display);
}

void LvglUi::initMainScreen() {
    _mainScreen = lv_obj_create(nullptr);

    lv_obj_set_style_text_font(_mainScreen, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(_mainScreen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_mainScreen, LV_OPA_COVER, 0);

    auto* tempoLabel = lv_label_create(_mainScreen);
    lv_obj_set_pos(tempoLabel, 10, 10);
    lv_obj_set_style_text_color(tempoLabel, lv_color_hex(0xFF0000), 0);
    lv_label_bind_text(tempoLabel, &_tempoSubject, "Tempo: %d");

    auto* swingLabel = lv_label_create(_mainScreen);
    lv_obj_set_pos(swingLabel, 10, 30);
    lv_obj_set_style_text_color(swingLabel, lv_color_hex(0x00FF00), 0);
    lv_label_bind_text(swingLabel, &_swingSubject, "Swing: %d");

    auto* volumeLabel = lv_label_create(_mainScreen);
    lv_obj_set_pos(volumeLabel, 10, 50);
    lv_obj_set_style_text_color(volumeLabel, lv_color_hex(0x0000FF), 0);
    lv_label_bind_text(volumeLabel, &_volumeSubject, "Volume: %d");

    _externalClockLabel = lv_label_create(_mainScreen);
    lv_obj_set_pos(_externalClockLabel, 10, 70);
    lv_obj_set_style_text_color(_externalClockLabel, lv_color_hex(0x00FFFF), 0);
    lv_label_set_text(_externalClockLabel, "Clock: Waiting");

    lv_obj_add_event_cb(_mainScreen, onMainScreenLoaded, LV_EVENT_SCREEN_LOADED, this);

    // lv_subject_add_observer_obj(&_sequencerStepsSubject, onStepsChanged, volumeLabel, this);
    lv_subject_add_observer(&_sequencerStepsSubject, onStepsChanged, this);
}

void LvglUi::setExternalClock(SwingMetro::ExternalMidiClockStatus status, uint8_t tempo) {
    if (status == _displayedExternalClockStatus && tempo == _displayedExternalTempo) {
        return;
    }
    _displayedExternalClockStatus = status;
    _displayedExternalTempo = tempo;
    static constexpr const char* names[] = {"Waiting", "Locked", "Lost"};
    if (tempo == 0) {
        lv_label_set_text_fmt(_externalClockLabel, "Clock: %s",
                              names[static_cast<uint8_t>(status)]);
    } else {
        lv_label_set_text_fmt(_externalClockLabel, "Clock: %s %u BPM",
                              names[static_cast<uint8_t>(status)], static_cast<unsigned>(tempo));
    }
}

void LvglUi::initStepSettingsScreen() {
    constexpr int16_t velocityLabelY = 80;
    constexpr int16_t gateLabelY = 110;
    _stepSettingsScreen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_stepSettingsScreen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_stepSettingsScreen, LV_OPA_COVER, 0);

    _selectedStepLabel = lv_label_create(_stepSettingsScreen);
    lv_obj_set_pos(_selectedStepLabel, 10, 20);
    lv_obj_set_style_text_color(_selectedStepLabel, lv_color_hex(0xF88C00), 0);
    lv_label_set_text(_selectedStepLabel, "Step 1");

    _selectedNoteLabel = lv_label_create(_stepSettingsScreen);
    lv_obj_set_pos(_selectedNoteLabel, 10, 50);
    lv_obj_set_style_text_color(_selectedNoteLabel, lv_color_white(), 0);
    lv_label_set_text(_selectedNoteLabel, "Note: C0");

    _selectedVelocityLabel = lv_label_create(_stepSettingsScreen);
    lv_obj_set_pos(_selectedVelocityLabel, 10, velocityLabelY);
    lv_obj_set_style_text_color(_selectedVelocityLabel, lv_color_white(), 0);
    lv_label_set_text(_selectedVelocityLabel, "Velocity: 127");

    _selectedGateLabel = lv_label_create(_stepSettingsScreen);
    lv_obj_set_pos(_selectedGateLabel, 10, gateLabelY);
    lv_obj_set_style_text_color(_selectedGateLabel, lv_color_white(), 0);
    lv_label_set_text(_selectedGateLabel, "Gate: 100%");
}

void LvglUi::initMidiClockModal() {
    _midiClockModal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_midiClockModal, 112, 132);
    lv_obj_align(_midiClockModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_midiClockModal, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(_midiClockModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_midiClockModal, lv_color_hex(0xF88C00), 0);
    lv_obj_set_style_border_width(_midiClockModal, 2, 0);
    lv_obj_set_style_pad_all(_midiClockModal, 6, 0);
    lv_obj_remove_flag(_midiClockModal, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(_midiClockModal);
    lv_label_set_text(title, "MIDI Clock");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF88C00), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    _midiClockActiveLabel = lv_label_create(_midiClockModal);
    lv_obj_align(_midiClockActiveLabel, LV_ALIGN_TOP_MID, 0, 20);

    static constexpr const char* modeNames[] = {"Off", "Internal", "External"};
    for (uint8_t index = 0; index < _midiClockModeLabels.size(); ++index) {
        auto* label = lv_label_create(_midiClockModal);
        _midiClockModeLabels[index] = label;
        lv_label_set_text(label, modeNames[index]);
        lv_obj_set_width(label, 92);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, static_cast<int16_t>(42 + index * 24));
    }
    lv_obj_add_flag(_midiClockModal, LV_OBJ_FLAG_HIDDEN);
}

void LvglUi::setMidiClockModal(bool open, SwingMetro::MidiClockMode active,
                               SwingMetro::MidiClockMode preview) {
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
                  preview == _displayedMidiClockPreview)) {
        return;
    }

    static constexpr const char* modeNames[] = {"Off", "Internal", "External"};
    _displayedMidiClockActive = active;
    _displayedMidiClockPreview = preview;
    lv_label_set_text_fmt(_midiClockActiveLabel, "Active: %s",
                          modeNames[static_cast<uint8_t>(active)]);
    for (uint8_t index = 0; index < _midiClockModeLabels.size(); ++index) {
        const bool selected = index == static_cast<uint8_t>(preview);
        lv_obj_set_style_text_color(_midiClockModeLabels[index],
                                    selected ? lv_color_hex(0xFFFF00) : lv_color_white(), 0);
        lv_obj_set_style_bg_color(_midiClockModeLabels[index],
                                  selected ? lv_color_hex(0x404000) : lv_color_black(), 0);
        lv_obj_set_style_bg_opa(_midiClockModeLabels[index],
                                selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

void LvglUi::initProgramStorageModal() {
    _programStorageModal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_programStorageModal, 112, 100);
    lv_obj_align(_programStorageModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_programStorageModal, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(_programStorageModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_programStorageModal, lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_border_width(_programStorageModal, 2, 0);
    lv_obj_remove_flag(_programStorageModal, LV_OBJ_FLAG_SCROLLABLE);
    _programStorageTitleLabel = lv_label_create(_programStorageModal);
    lv_obj_align(_programStorageTitleLabel, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_color(_programStorageTitleLabel, lv_color_white(), 0);
    _programStorageValueLabel = lv_label_create(_programStorageModal);
    lv_obj_align(_programStorageValueLabel, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_style_text_color(_programStorageValueLabel, lv_color_white(), 0);

    static constexpr const char* actionNames[] = {"Save", "Load"};
    for (uint8_t index = 0; index < _programStorageActionLabels.size(); ++index) {
        auto* label = lv_label_create(_programStorageModal);
        _programStorageActionLabels[index] = label;
        lv_label_set_text(label, actionNames[index]);
        lv_obj_set_width(label, 92);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, static_cast<int16_t>(36 + index * 24));
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(_programStorageModal, LV_OBJ_FLAG_HIDDEN);
}

void LvglUi::setProgramStorageModal(SwingMetro::ProgramStorageModalState state,
                                    SwingMetro::ProgramStorageAction action, uint8_t slot,
                                    SwingMetro::ProgramStoreStatus status) {
    if (state == _displayedProgramStorageState && action == _displayedProgramStorageAction &&
        slot == _displayedProgramStorageSlot && status == _displayedProgramStorageStatus) {
        return;
    }
    _displayedProgramStorageState = state;
    _displayedProgramStorageAction = action;
    _displayedProgramStorageSlot = slot;
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
            const bool selected = index == static_cast<uint8_t>(action);
            lv_obj_remove_flag(_programStorageActionLabels[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(_programStorageActionLabels[index],
                                        selected ? lv_color_hex(0xFFFF00) : lv_color_white(), 0);
            lv_obj_set_style_bg_color(_programStorageActionLabels[index],
                                      selected ? lv_color_hex(0x404000) : lv_color_black(), 0);
            lv_obj_set_style_bg_opa(_programStorageActionLabels[index],
                                    selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
    } else if (state == SwingMetro::ProgramStorageModalState::Slot) {
        lv_obj_remove_flag(_programStorageValueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_programStorageTitleLabel,
                          action == SwingMetro::ProgramStorageAction::Save ? "Save slot"
                                                                           : "Load slot");
        lv_label_set_text_fmt(_programStorageValueLabel, "Slot %u", static_cast<unsigned>(slot));
    } else if (state == SwingMetro::ProgramStorageModalState::Busy) {
        lv_obj_remove_flag(_programStorageValueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_programStorageTitleLabel, "Program Storage");
        lv_label_set_text(_programStorageValueLabel,
                          action == SwingMetro::ProgramStorageAction::Save ? "Saving..."
                                                                           : "Loading...");
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

void LvglUi::drawSequencerSteps() {
    auto rawValue = static_cast<uint32_t>(lv_subject_get_int(&_sequencerStepsSubject));
    drawSequencerSteps(rawValue);
}

void LvglUi::drawSequencerSteps(uint32_t rawValue) {
    constexpr int16_t stepSize = 9;
    constexpr int16_t firstStepY = 105;

    std::bitset<SEQUENCER_STEPS_COUNT> enabledSteps{static_cast<uint16_t>(rawValue)};
    uint8_t activeStep = rawValue >> 16;

    for (uint8_t stepNumber = 0; stepNumber < enabledSteps.size(); ++stepNumber) {
        const uint8_t row = stepNumber / 8;
        const uint8_t column = stepNumber % 8;

        const bool isEnabled = enabledSteps[stepNumber];
        const bool isActive = stepNumber == activeStep;

        auto* step = _stepSquares[stepNumber];

        if (step == nullptr) {
            _stepSquares[stepNumber] = lv_obj_create(_mainScreen);
            step = _stepSquares[stepNumber];
            lv_obj_set_size(step, stepSize, stepSize);
            lv_obj_set_pos(step, 1 + (column * (stepSize + 1)),
                           firstStepY + (row * (stepSize + 1)));

            lv_obj_set_style_bg_opa(step, LV_OPA_COVER, 0);

            lv_obj_set_style_border_width(step, 1, 0);
            lv_obj_set_style_radius(step, 0, 0);
            lv_obj_set_style_pad_all(step, 0, 0);
            lv_obj_remove_flag(step, LV_OBJ_FLAG_SCROLLABLE);
        }

        auto backgroundColor = isEnabled ? lv_color_hex(0xF88C00) : lv_color_black();

        lv_obj_set_style_bg_color(step, backgroundColor, 0);

        auto borderColor = isActive ? lv_color_hex(0xFF0000) : lv_color_hex(0xFFFF00);

        lv_obj_set_style_border_color(step, borderColor, 0);
    }
}

void LvglUi::onMainScreenLoaded(lv_event_t* event) {
    auto& self = *static_cast<LvglUi*>(lv_event_get_user_data(event));
    self.drawSequencerSteps();
}

void LvglUi::onStepsChanged(lv_observer_t* observer, lv_subject_t* subject) {
    auto& self = *static_cast<LvglUi*>(lv_observer_get_user_data(observer));
    auto rawValue = static_cast<uint32_t>(lv_subject_get_int(subject));
    self.drawSequencerSteps(rawValue);
}
