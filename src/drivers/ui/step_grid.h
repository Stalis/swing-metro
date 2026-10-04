#pragma once

#include "components/ui_snapshot.h"

#include <array>
#include <cstdint>

#include <lvgl.h>

constexpr std::uint8_t SEQUENCER_STEPS_COUNT = 16;

class StepGrid {
  public:
    void init(lv_obj_t* parent);
    void apply(const UiSettings::Main& settings);

  private:
    struct Cell {
        lv_obj_t* frame = nullptr;
        lv_obj_t* number = nullptr;
        lv_obj_t* note = nullptr;
        lv_obj_t* velocityLabel = nullptr;
        lv_obj_t* velocity = nullptr;
        lv_obj_t* gateLabel = nullptr;
        lv_obj_t* gate = nullptr;
        lv_obj_t* off = nullptr;
        std::uint64_t renderKey = UINT64_MAX;
    };

    std::array<Cell, SEQUENCER_STEPS_COUNT> _cells{};
};
