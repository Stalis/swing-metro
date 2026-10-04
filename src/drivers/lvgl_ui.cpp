#include "lvgl_ui.h"

#include "engine/stage5_instrumentation.h"
#include "ui/ui_fonts.h"

void LvglUi::setup() {
    _display.setup();
    UiFonts::initialize();

    _mainScreen.create();
    _backdrop.create();
    _stepSettingsScreen.create();
    _midiClockDialog.create();
    _programStorageDialog.create();
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
    const bool storageOpen = values.storage.state != SwingMetro::ProgramStorageModalState::Closed;
    const bool midiOpen = !storageOpen && values.midiClock.modalOpen;
    const bool stepOpen = !storageOpen && !midiOpen && values.page == UiPage::StepSettings;
    _backdrop.setVisible(storageOpen || midiOpen || stepOpen);
    _stepSettingsScreen.setVisible(stepOpen);
    if (stepOpen) {
        _stepSettingsScreen.apply(values.editor);
    }
    auto midi = values.midiClock;
    midi.modalOpen = midiOpen;
    _midiClockDialog.apply(midi);
    _programStorageDialog.apply(values.storage);
}
