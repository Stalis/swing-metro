#pragma once

#include <cstdint>

#include <lvgl.h>

#include "components/ui_snapshot.h"

class StepSettingsScreen {
  public:
    StepSettingsScreen() = default;

    StepSettingsScreen(const StepSettingsScreen&) = delete;
    StepSettingsScreen& operator=(const StepSettingsScreen&) = delete;
    StepSettingsScreen(StepSettingsScreen&&) = delete;
    StepSettingsScreen& operator=(StepSettingsScreen&&) = delete;

    void create();
    [[nodiscard]] lv_obj_t* root() const { return _root; }
    void apply(const UiSettings::Editor& settings);

  private:
    lv_obj_t* _root = nullptr;
    lv_obj_t* _selectedStepLabel = nullptr;
    lv_obj_t* _selectedNoteLabel = nullptr;
    lv_obj_t* _selectedVelocityLabel = nullptr;
    lv_obj_t* _selectedGateLabel = nullptr;
    uint8_t _displayedStep = UINT8_MAX;
    uint8_t _displayedNote = UINT8_MAX;
    uint8_t _displayedVelocity = UINT8_MAX;
    uint8_t _displayedGate = UINT8_MAX;
};
