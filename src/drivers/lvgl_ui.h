#pragma once

#include <array>
#include <lvgl.h>

#include "components/ui_view_model.h"
#include "engine/runtime_timing_diagnostics.h"
#include "ui/pico_display.h"
#include "ui/step_grid.h"

class LvglUi {
  public:
    explicit LvglUi(SwingMetro::RuntimeTimingDiagnostics& runtimeTimingDiagnostics)
        : _runtimeTimingDiagnostics(runtimeTimingDiagnostics), _display(runtimeTimingDiagnostics) {}

    LvglUi(const LvglUi&) = delete;
    LvglUi& operator=(const LvglUi&) = delete;
    LvglUi(LvglUi&&) = delete;
    LvglUi& operator=(LvglUi&&) = delete;

    void setup();
    void loop();

    void readViewModel(const UiViewModel& viewModel);

    void setTempo(uint8_t);
    void setSwing(uint8_t);
    void setVolume(uint8_t);
    void setSteps(std::bitset<SEQUENCER_STEPS_COUNT> stepsState, uint8_t activeStep);

  private:
    SwingMetro::RuntimeTimingDiagnostics& _runtimeTimingDiagnostics;
    PicoDisplay _display;

    // SCREENS
    lv_obj_t* _mainScreen;
    lv_obj_t* _stepSettingsScreen;
    lv_obj_t* _selectedStepLabel;
    lv_obj_t* _selectedNoteLabel;
    lv_obj_t* _selectedVelocityLabel;
    lv_obj_t* _selectedGateLabel;
    UiPage _currentPage = UiPage::MainDisplay;
    uint8_t _displayedStep = UINT8_MAX;
    uint8_t _displayedNote = UINT8_MAX;
    uint8_t _displayedVelocity = UINT8_MAX;
    uint8_t _displayedGate = UINT8_MAX;
    lv_obj_t* _midiClockModal;
    lv_obj_t* _midiClockActiveLabel;
    std::array<lv_obj_t*, 4> _midiClockModeLabels{};
    bool _midiClockModalVisible = false;
    SwingMetro::MidiClockMode _displayedMidiClockActive = SwingMetro::MidiClockMode::Off;
    SwingMetro::MidiClockMenuItem _displayedMidiClockSelection = SwingMetro::MidiClockMenuItem::Off;
    lv_obj_t* _externalClockLabel;
    SwingMetro::ExternalMidiClockStatus _displayedExternalClockStatus =
        SwingMetro::ExternalMidiClockStatus::Waiting;
    uint8_t _displayedExternalTempo = UINT8_MAX;
    lv_obj_t* _programStorageModal;
    lv_obj_t* _programStorageTitleLabel;
    lv_obj_t* _programStorageValueLabel;
    std::array<lv_obj_t*, 4> _programStorageActionLabels{};
    SwingMetro::ProgramStorageModalState _displayedProgramStorageState =
        SwingMetro::ProgramStorageModalState::Closed;
    SwingMetro::ProgramStorageAction _displayedProgramStorageAction =
        SwingMetro::ProgramStorageAction::Save;
    SwingMetro::ProgramStorageMenuItem _displayedProgramStorageSelection =
        SwingMetro::ProgramStorageMenuItem::Save;
    uint8_t _displayedProgramStorageSlot = UINT8_MAX;
    SwingMetro::ProgramResetChoice _displayedProgramResetChoice =
        SwingMetro::ProgramResetChoice::No;
    SwingMetro::ProgramStoreStatus _displayedProgramStorageStatus =
        SwingMetro::ProgramStoreStatus::Ok;

    StepGrid _stepGrid;

    void initMainScreen();
    void initStepSettingsScreen();
    void initMidiClockModal();
    void setMidiClockModal(bool open, SwingMetro::MidiClockMode active,
                           SwingMetro::MidiClockMenuItem selection);
    void setExternalClock(SwingMetro::ExternalMidiClockStatus status, uint8_t tempo);
    void initProgramStorageModal();
    void setProgramStorageModal(SwingMetro::ProgramStorageModalState state,
                                SwingMetro::ProgramStorageMenuItem selection,
                                SwingMetro::ProgramStorageAction action, uint8_t slot,
                                SwingMetro::ProgramResetChoice resetChoice,
                                SwingMetro::ProgramStoreStatus status);
    // DATA
    lv_subject_t _tempoSubject;
    lv_subject_t _swingSubject;
    lv_subject_t _volumeSubject;

    // HANDLERS
    static void onMainScreenLoaded(lv_event_t* event);
};
