#pragma once

#include <array>
#include <lvgl.h>

#include "components/ui_view_model.h"
#include "engine/runtime_timing_diagnostics.h"
#include "ui/main_screen.h"
#include "ui/pico_display.h"
#include "ui/step_settings_screen.h"

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

  private:
    SwingMetro::RuntimeTimingDiagnostics& _runtimeTimingDiagnostics;
    PicoDisplay _display;

    MainScreen _mainScreen;
    StepSettingsScreen _stepSettingsScreen;
    UiPage _currentPage = UiPage::MainDisplay;
    lv_obj_t* _midiClockModal;
    lv_obj_t* _midiClockActiveLabel;
    std::array<lv_obj_t*, 4> _midiClockModeLabels{};
    bool _midiClockModalVisible = false;
    SwingMetro::MidiClockMode _displayedMidiClockActive = SwingMetro::MidiClockMode::Off;
    SwingMetro::MidiClockMenuItem _displayedMidiClockSelection = SwingMetro::MidiClockMenuItem::Off;
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

    void initMidiClockModal();
    void setMidiClockModal(bool open, SwingMetro::MidiClockMode active,
                           SwingMetro::MidiClockMenuItem selection);
    void initProgramStorageModal();
    void setProgramStorageModal(SwingMetro::ProgramStorageModalState state,
                                SwingMetro::ProgramStorageMenuItem selection,
                                SwingMetro::ProgramStorageAction action, uint8_t slot,
                                SwingMetro::ProgramResetChoice resetChoice,
                                SwingMetro::ProgramStoreStatus status);
};
