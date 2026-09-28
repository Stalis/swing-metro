#pragma once

#include <cstdint>

#include <lvgl.h>

#include "components/ui_snapshot.h"
#include "step_grid.h"

class MainScreen {
  public:
    MainScreen() = default;

    MainScreen(const MainScreen&) = delete;
    MainScreen& operator=(const MainScreen&) = delete;
    MainScreen(MainScreen&&) = delete;
    MainScreen& operator=(MainScreen&&) = delete;

    void create();
    [[nodiscard]] lv_obj_t* root() const { return _root; }
    void apply(const UiSettings::Main& settings);

  private:
    lv_obj_t* _root = nullptr;
    lv_obj_t* _externalClockLabel = nullptr;
    lv_subject_t _tempoSubject;
    lv_subject_t _swingSubject;
    lv_subject_t _volumeSubject;
    SwingMetro::ExternalMidiClockStatus _displayedExternalClockStatus =
        SwingMetro::ExternalMidiClockStatus::Waiting;
    uint8_t _displayedExternalTempo = UINT8_MAX;
    StepGrid _stepGrid;

    void setExternalClock(SwingMetro::ExternalMidiClockStatus status, uint8_t tempo);
    static void onScreenLoaded(lv_event_t* event);
};
