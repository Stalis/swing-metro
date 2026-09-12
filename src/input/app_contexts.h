#pragma once

#include "app_input.h"

#include <cstdint>
#include <variant>

namespace SwingMetro {

class GlobalContext {
  public:
    [[nodiscard]] auto handle(const InputEvent& input) const
        -> ContextInput::DispatchResult<AppEvent> {
        using Result = ContextInput::DispatchResult<AppEvent>;

        if (input.source == InputId::TempoSwitch) {
            const auto* button = std::get_if<ContextInput::ButtonInput>(&input.payload);
            if (button != nullptr && button->phase == ContextInput::ButtonPhase::Pressed) {
                return Result::emit(AppEvent{ToggleTransport{}});
            }
        }

        if (input.source == InputId::ShiftSwitch) {
            const auto* trigger = std::get_if<ContextInput::TriggerInput>(&input.payload);
            if (trigger != nullptr) {
                return trigger->active ? Result::emit(AppEvent{ActivateShift{}})
                                       : Result::emit(AppEvent{DeactivateShift{}});
            }
        }

        return Result::pass();
    }
};

class StepSettingsContext {
  public:
    auto setSelectedStep(std::uint8_t step) noexcept -> void { selectedStep_ = step; }

    [[nodiscard]] auto handle(const InputEvent& input) const
        -> ContextInput::DispatchResult<AppEvent> {
        using Result = ContextInput::DispatchResult<AppEvent>;

        if (const auto* encoder = std::get_if<ContextInput::EncoderInput>(&input.payload)) {
            if (input.source == InputId::TempoEncoder) {
                return Result::emit(AppEvent{AdjustNote{encoder->delta}});
            }
            if (input.source == InputId::SwingEncoder || input.source == InputId::VolumeEncoder) {
                return Result::consume();
            }
            return Result::pass();
        }

        const auto* button = std::get_if<ContextInput::ButtonInput>(&input.payload);
        const auto step = stepIndexFromInputId(input.source);
        if (button == nullptr || !step.has_value()) {
            return Result::pass();
        }

        if (button->phase == ContextInput::ButtonPhase::Pressed ||
            button->phase == ContextInput::ButtonPhase::Clicked) {
            if (*step == selectedStep_) {
                return Result::consume();
            }
            return Result::emit(AppEvent{OpenStepSettings{*step}});
        }

        if (button->phase == ContextInput::ButtonPhase::LongPressed) {
            if (*step == selectedStep_) {
                return Result::emit(AppEvent{CloseStepSettings{}});
            }
            return Result::emit(AppEvent{OpenStepSettings{*step}});
        }

        return Result::pass();
    }

  private:
    std::uint8_t selectedStep_ = 0;
};

class ShiftContext {
  public:
    [[nodiscard]] auto handle(const InputEvent&) const -> ContextInput::DispatchResult<AppEvent> {
        return ContextInput::DispatchResult<AppEvent>::pass();
    }
};

} // namespace SwingMetro
