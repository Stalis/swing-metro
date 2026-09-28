#pragma once

#include "components/ui_view_model.h"
#include "engine/runtime_timing_diagnostics.h"
#include "ui/main_screen.h"
#include "ui/midi_clock_dialog.h"
#include "ui/pico_display.h"
#include "ui/program_storage_dialog.h"
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
    MidiClockDialog _midiClockDialog;
    ProgramStorageDialog _programStorageDialog;
    UiPage _currentPage = UiPage::MainDisplay;
};
