#pragma once

#include "components/ui_snapshot.h"
#include "engine/sequencer.h"

#include <cstdint>
#include <optional>

namespace SwingMetro {

class StepEditor {
  public:
    explicit StepEditor(Sequencer& sequencer) : _sequencer{sequencer} {}

    auto adjustNote(std::optional<std::uint8_t> selectedStep, std::int16_t delta) -> void {
        if (selectedStep.has_value()) {
            (void)_sequencer.adjustStepNote(*selectedStep, delta);
        }
    }

    auto adjustVelocity(std::optional<std::uint8_t> selectedStep, std::int8_t delta) -> void {
        if (selectedStep.has_value()) {
            (void)_sequencer.adjustStepVelocity(*selectedStep, delta);
        }
    }

    auto adjustGate(std::optional<std::uint8_t> selectedStep, std::int8_t delta) -> void {
        if (selectedStep.has_value()) {
            (void)_sequencer.adjustStepGate(*selectedStep, delta);
        }
    }

    [[nodiscard]] auto snapshot(std::optional<std::uint8_t> selectedStep, bool shiftActive) const
        -> UiSettings::Editor {
        UiSettings::Editor editor;
        editor.selectedStep = selectedStep.value_or(UINT8_MAX);
        editor.selectedNote =
            selectedStep.has_value() ? _sequencer.getStepMidiNote(*selectedStep).value_or(36) : 36;
        editor.selectedVelocity = selectedStep.has_value()
                                      ? _sequencer.getStepVelocity(*selectedStep).value_or(127)
                                      : 127;
        editor.selectedGate =
            selectedStep.has_value()
                ? _sequencer.getStepGate(*selectedStep).value_or(STEP_DEFAULT_GATE)
                : STEP_DEFAULT_GATE;
        editor.transportRunning = _sequencer.isRunning();
        editor.shiftActive = shiftActive;
        return editor;
    }

  private:
    Sequencer& _sequencer;
};

} // namespace SwingMetro
