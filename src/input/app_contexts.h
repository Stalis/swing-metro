#pragma once

#include "app_input.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace SwingMetro {

class GlobalContext {
  public:
    [[nodiscard]] auto handle(const InputEvent& input) const
        -> ContextInput::DispatchResult<AppEvent> {
        using Result = ContextInput::DispatchResult<AppEvent>;

        if (input.source == InputId::TempoSwitch) {
            const auto* button = std::get_if<ContextInput::ButtonInput>(&input.payload);
            if (button != nullptr) {
                if (button->phase == ContextInput::ButtonPhase::LongPressed) {
                    return Result::emit(AppEvent{OpenMidiClockSettings{}});
                }
                if (button->phase == ContextInput::ButtonPhase::Clicked) {
                    return Result::emit(AppEvent{ToggleTransport{}});
                }
                return Result::consume();
            }
        }

        if (input.source == InputId::ShiftSwitch) {
            const auto* trigger = std::get_if<ContextInput::TriggerInput>(&input.payload);
            if (trigger != nullptr) {
                return trigger->active ? Result::emit(AppEvent{ActivateShift{}})
                                       : Result::emit(AppEvent{DeactivateShift{}});
            }
        }

        if (input.source == InputId::VolumeEncoder) {
            if (const auto* button = std::get_if<ContextInput::ButtonInput>(&input.payload)) {
                if (button->phase == ContextInput::ButtonPhase::LongPressed) {
                    return Result::emit(AppEvent{OpenProgramStorage{}});
                }
                if (button->phase == ContextInput::ButtonPhase::Pressed) {
                    return Result::emit(AppEvent{ActivateShift{}});
                }
                if (button->phase == ContextInput::ButtonPhase::Clicked ||
                    button->phase == ContextInput::ButtonPhase::Released) {
                    return Result::emit(AppEvent{DeactivateShift{}});
                }
                return Result::consume();
            }
        }

        return Result::pass();
    }
};

class MidiClockSettingsContext {
  public:
    [[nodiscard]] auto handle(const InputEvent& input) const
        -> ContextInput::DispatchResult<AppEvent> {
        using Result = ContextInput::DispatchResult<AppEvent>;

        if (input.source == InputId::TempoEncoder) {
            if (const auto* encoder = std::get_if<ContextInput::EncoderInput>(&input.payload)) {
                return Result::emit(AppEvent{AdjustMidiClockPreview{encoder->delta}});
            }
        }
        if (input.source == InputId::TempoSwitch) {
            if (const auto* button = std::get_if<ContextInput::ButtonInput>(&input.payload)) {
                if (button->phase == ContextInput::ButtonPhase::Clicked) {
                    return Result::emit(AppEvent{ConfirmMidiClockSettings{}});
                }
            }
        }
        return Result::consume();
    }
};

class StepSettingsContext {
  public:
    auto setSelectedStep(std::uint8_t step) noexcept -> void { _selectedStep = step; }

    [[nodiscard]] auto handle(const InputEvent& input) const
        -> ContextInput::DispatchResult<AppEvent> {
        using Result = ContextInput::DispatchResult<AppEvent>;

        if (const auto* encoder = std::get_if<ContextInput::EncoderInput>(&input.payload)) {
            if (input.source == InputId::TempoEncoder) {
                return Result::emit(AppEvent{AdjustNote{encoder->delta}});
            }
            if (input.source == InputId::SwingEncoder) {
                return Result::emit(AppEvent{AdjustVelocity{encoder->delta}});
            }
            if (input.source == InputId::VolumeEncoder) {
                return Result::emit(AppEvent{AdjustGate{encoder->delta}});
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
            if (*step == _selectedStep) {
                return Result::consume();
            }
            return Result::emit(AppEvent{OpenStepSettings{*step}});
        }

        if (button->phase == ContextInput::ButtonPhase::LongPressed) {
            if (*step == _selectedStep) {
                return Result::emit(AppEvent{CloseStepSettings{}});
            }
            return Result::emit(AppEvent{OpenStepSettings{*step}});
        }

        return Result::pass();
    }

  private:
    std::uint8_t _selectedStep = 0;
};

class ShiftContext {
  public:
    explicit ShiftContext(const std::optional<std::uint8_t>& selectedStep) noexcept
        : _selectedStep{selectedStep} {}

    [[nodiscard]] auto handle(const InputEvent& input) const
        -> ContextInput::DispatchResult<AppEvent> {
        using Result = ContextInput::DispatchResult<AppEvent>;
        if (_selectedStep.has_value() && input.source == InputId::TempoEncoder) {
            if (const auto* encoder = std::get_if<ContextInput::EncoderInput>(&input.payload)) {
                const auto semitones = static_cast<std::int16_t>(
                    static_cast<std::int16_t>(encoder->delta) * NOTES_IN_OCTAVE);
                return Result::emit(AppEvent{AdjustNote{semitones}});
            }
        }
        return Result::pass();
    }

  private:
    const std::optional<std::uint8_t>& _selectedStep;
};

class ProgramStorageContext {
  public:
    auto setState(ProgramStorageModalState state) noexcept -> void { _state = state; }

    [[nodiscard]] auto handle(const InputEvent& input) const
        -> ContextInput::DispatchResult<AppEvent> {
        using Result = ContextInput::DispatchResult<AppEvent>;
        if (input.source == InputId::VolumeEncoder) {
            if (const auto* encoder = std::get_if<ContextInput::EncoderInput>(&input.payload)) {
                if (_state == ProgramStorageModalState::Action) {
                    return Result::emit(AppEvent{SelectProgramStorageAction{encoder->delta}});
                }
                if (_state == ProgramStorageModalState::Slot) {
                    return Result::emit(AppEvent{SelectProgramStorageSlot{encoder->delta}});
                }
            }
            if (const auto* button = std::get_if<ContextInput::ButtonInput>(&input.payload)) {
                if (button->phase == ContextInput::ButtonPhase::Clicked) {
                    if (_state == ProgramStorageModalState::Action) {
                        return Result::emit(AppEvent{ConfirmProgramStorageAction{}});
                    }
                    if (_state == ProgramStorageModalState::Slot) {
                        return Result::emit(AppEvent{ConfirmProgramStorageSlot{}});
                    }
                    if (_state == ProgramStorageModalState::Success ||
                        _state == ProgramStorageModalState::Error) {
                        return Result::emit(AppEvent{CloseProgramStorage{}});
                    }
                }
            }
        }
        return Result::consume();
    }

  private:
    ProgramStorageModalState _state = ProgramStorageModalState::Closed;
};

} // namespace SwingMetro
