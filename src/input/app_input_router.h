#pragma once

#include "app_contexts.h"
#include "main_display_context.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace SwingMetro {

template <std::size_t Capacity>
class AppInputRouter {
    static_assert(Capacity >= 2, "AppInputRouter needs global and main contexts");

  public:
    AppInputRouter() : _shiftContext{_selectedStep} {
        (void)_router.addContext(_globalContext);
        (void)_router.addContext(_mainContext);
    }

    auto dispatch(const InputEvent& input) -> std::optional<AppEvent> {
        const auto source = static_cast<std::uint8_t>(input.source);
        const auto* button = std::get_if<ContextInput::ButtonInput>(&input.payload);
        if (button != nullptr && _capturedButtons.test(source)) {
            if (button->phase == ContextInput::ButtonPhase::Released) {
                _capturedButtons.reset(source);
            }
            return std::nullopt;
        }

        const auto result = _router.dispatch(input);
        return result.hasEvent() ? std::optional<AppEvent>{result.event()} : std::nullopt;
    }

    auto captureButton(const InputEvent* sourceInput) -> void {
        if (sourceInput == nullptr) {
            return;
        }

        const auto* button = std::get_if<ContextInput::ButtonInput>(&sourceInput->payload);
        if (button != nullptr && button->phase != ContextInput::ButtonPhase::Released) {
            _capturedButtons.set(static_cast<std::uint8_t>(sourceInput->source));
        }
    }

    [[nodiscard]] auto openStepSettings(std::uint8_t step) -> bool {
        if (step >= STEPS_COUNT) {
            return false;
        }

        if (_selectedStep.has_value()) {
            if (*_selectedStep == step) {
                return false;
            }
            _selectedStep = step;
            _stepContext.setSelectedStep(step);
            return true;
        }

        if (_router.size() == _router.capacity()) {
            return false;
        }

        const bool shiftWasActive = isShiftActive();
        if (shiftWasActive) {
            (void)_router.releaseContext(_shiftContext);
        }
        const auto added = _router.addContext(_stepContext);
        if (added != ContextInput::AddContextResult::Added) {
            if (shiftWasActive) {
                (void)_router.addContext(_shiftContext);
            }
            return false;
        }
        if (shiftWasActive) {
            (void)_router.addContext(_shiftContext);
        }
        _selectedStep = step;
        _stepContext.setSelectedStep(step);
        return true;
    }

    [[nodiscard]] auto closeStepSettings() -> bool {
        if (!_selectedStep.has_value() ||
            _router.releaseContext(_stepContext) != ContextInput::ReleaseContextResult::Released) {
            return false;
        }
        _selectedStep.reset();
        return true;
    }

    auto activateShift() -> void { (void)_router.addContext(_shiftContext); }
    auto deactivateShift() -> void { (void)_router.releaseContext(_shiftContext); }

    [[nodiscard]] auto openMidiClockSettings() -> bool {
        const bool shiftWasActive = isShiftActive();
        if (shiftWasActive) {
            (void)_router.releaseContext(_shiftContext);
        } else if (_router.size() == _router.capacity()) {
            return false;
        }

        if (_router.addContext(_midiClockContext) != ContextInput::AddContextResult::Added) {
            if (shiftWasActive) {
                (void)_router.addContext(_shiftContext);
            }
            return false;
        }

        _restoreShiftAfterMidiClockModal = shiftWasActive;
        return true;
    }

    [[nodiscard]] auto closeMidiClockSettings() -> bool {
        if (_router.releaseContext(_midiClockContext) !=
            ContextInput::ReleaseContextResult::Released) {
            return false;
        }

        if (_restoreShiftAfterMidiClockModal) {
            (void)_router.addContext(_shiftContext);
        }
        _restoreShiftAfterMidiClockModal = false;
        return true;
    }

    [[nodiscard]] auto openProgramStorage() -> bool {
        if (_router.size() == _router.capacity()) {
            return false;
        }
        (void)_router.releaseContext(_shiftContext);
        return _router.addContext(_programStorageContext) == ContextInput::AddContextResult::Added;
    }

    [[nodiscard]] auto closeProgramStorage() -> bool {
        return _router.releaseContext(_programStorageContext) ==
               ContextInput::ReleaseContextResult::Released;
    }

    auto setProgramStorageState(ProgramStorageModalState state) noexcept -> void {
        _programStorageContext.setState(state);
    }

    [[nodiscard]] auto selectedStep() const noexcept -> std::optional<std::uint8_t> {
        return _selectedStep;
    }
    [[nodiscard]] auto isShiftActive() const noexcept -> bool {
        return _router.contains(_shiftContext);
    }
    [[nodiscard]] auto hasStepSettingsContext() const noexcept -> bool {
        return _router.contains(_stepContext);
    }
    [[nodiscard]] auto stackSize() const noexcept -> std::size_t { return _router.size(); }

  private:
    std::optional<std::uint8_t> _selectedStep;
    GlobalContext _globalContext;
    MainDisplayContext _mainContext;
    StepSettingsContext _stepContext;
    ShiftContext _shiftContext;
    MidiClockSettingsContext _midiClockContext;
    ProgramStorageContext _programStorageContext;
    ContextInput::Router<InputEvent, AppEvent, Capacity> _router;
    std::bitset<256> _capturedButtons;
    bool _restoreShiftAfterMidiClockModal = false;
};

} // namespace SwingMetro
