#pragma once

#include <array>

#include <lvgl.h>

#include "components/ui_snapshot.h"

class MidiClockDialog {
  public:
    MidiClockDialog() = default;

    MidiClockDialog(const MidiClockDialog&) = delete;
    MidiClockDialog& operator=(const MidiClockDialog&) = delete;
    MidiClockDialog(MidiClockDialog&&) = delete;
    MidiClockDialog& operator=(MidiClockDialog&&) = delete;

    void create();
    void apply(const UiSettings::MidiClock& settings);

  private:
    lv_obj_t* _modal = nullptr;
    lv_obj_t* _activeLabel = nullptr;
    std::array<lv_obj_t*, 4> _modeLabels{};
    bool _visible = false;
    SwingMetro::MidiClockMode _displayedActive = SwingMetro::MidiClockMode::Off;
    SwingMetro::MidiClockMenuItem _displayedSelection = SwingMetro::MidiClockMenuItem::Off;
};
