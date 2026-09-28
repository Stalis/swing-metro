#include "lvgl_ui.h"

#include "engine/stage5_instrumentation.h"

void LvglUi::setup() {
    _display.setup();

    _mainScreen.create();
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
    if (values.page == UiPage::StepSettings) {
        _stepSettingsScreen.apply(values.editor);
    }

    if (values.page != _currentPage) {
        _currentPage = values.page;
        lv_screen_load(_currentPage == UiPage::StepSettings ? _stepSettingsScreen.root()
                                                            : _mainScreen.root());
    }

    _midiClockDialog.apply(values.midiClock);
    _programStorageDialog.apply(values.storage);
}
