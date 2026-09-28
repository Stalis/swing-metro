#include "step_grid.h"

#include "ui_theme.h"

void StepGrid::init(lv_obj_t* parent) {
    _parent = parent;
    lv_subject_init_int(&_subject, 0);
    lv_subject_add_observer(&_subject, onChanged, this);
}

void StepGrid::setSteps(std::bitset<SEQUENCER_STEPS_COUNT> stepsState, uint8_t activeStep) {
    lv_subject_set_int(&_subject,
                       stepsState.to_ulong() | (static_cast<uint16_t>(activeStep) << 16));
}

void StepGrid::draw() { draw(static_cast<uint32_t>(lv_subject_get_int(&_subject))); }

void StepGrid::draw(uint32_t rawValue) {
    constexpr int16_t stepSize = 9;
    constexpr int16_t firstStepY = 105;
    std::bitset<SEQUENCER_STEPS_COUNT> enabledSteps{static_cast<uint16_t>(rawValue)};
    const uint8_t activeStep = rawValue >> 16;

    for (uint8_t stepNumber = 0; stepNumber < enabledSteps.size(); ++stepNumber) {
        const uint8_t row = stepNumber / 8;
        const uint8_t column = stepNumber % 8;
        auto* step = _squares[stepNumber];
        if (step == nullptr) {
            _squares[stepNumber] = lv_obj_create(_parent);
            step = _squares[stepNumber];
            lv_obj_set_size(step, stepSize, stepSize);
            lv_obj_set_pos(step, 1 + (column * (stepSize + 1)),
                           firstStepY + (row * (stepSize + 1)));
            lv_obj_set_style_bg_opa(step, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(step, 1, 0);
            lv_obj_set_style_radius(step, 0, 0);
            lv_obj_set_style_pad_all(step, 0, 0);
            lv_obj_remove_flag(step, LV_OBJ_FLAG_SCROLLABLE);
        }

        lv_obj_set_style_bg_color(step,
                                  enabledSteps[stepNumber] ? lv_color_hex(UiTheme::ORANGE)
                                                           : lv_color_hex(UiTheme::BLACK),
                                  0);
        lv_obj_set_style_border_color(step,
                                      stepNumber == activeStep ? lv_color_hex(UiTheme::RED)
                                                               : lv_color_hex(UiTheme::YELLOW),
                                      0);
    }
}

void StepGrid::onChanged(lv_observer_t* observer, lv_subject_t* subject) {
    auto& self = *static_cast<StepGrid*>(lv_observer_get_user_data(observer));
    self.draw(static_cast<uint32_t>(lv_subject_get_int(subject)));
}
