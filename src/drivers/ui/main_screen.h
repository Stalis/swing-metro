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
    lv_obj_t* _tempoLabel = nullptr;
    lv_obj_t* _swingLabel = nullptr;
    lv_obj_t* _midiChannelsLabel = nullptr;
    lv_obj_t* _clockModes[3]{};
    bool _hasApplied = false;
    std::uint8_t _displayedTempo = 0;
    std::uint8_t _displayedSwing = 0;
    std::uint16_t _displayedMidiChannelMask = 0;
    SwingMetro::MidiClockMode _displayedClockMode = SwingMetro::MidiClockMode::Off;
    StepGrid _stepGrid;
};
