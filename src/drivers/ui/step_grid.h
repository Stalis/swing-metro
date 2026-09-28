#pragma once

#include <array>
#include <bitset>
#include <cstdint>

#include <lvgl.h>

constexpr const uint8_t SEQUENCER_STEPS_COUNT = 16;

class StepGrid {
  public:
    StepGrid() = default;

    StepGrid(const StepGrid&) = delete;
    StepGrid& operator=(const StepGrid&) = delete;
    StepGrid(StepGrid&&) = delete;
    StepGrid& operator=(StepGrid&&) = delete;

    void init(lv_obj_t* parent);
    void setSteps(std::bitset<SEQUENCER_STEPS_COUNT> stepsState, uint8_t activeStep);
    void draw();

  private:
    lv_obj_t* _parent = nullptr;
    std::array<lv_obj_t*, SEQUENCER_STEPS_COUNT> _squares{};
    lv_subject_t _subject;

    void draw(uint32_t rawValue);
    static void onChanged(lv_observer_t* observer, lv_subject_t* subject);
};
