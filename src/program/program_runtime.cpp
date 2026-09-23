#include "program/program_runtime.h"

namespace SwingMetro {

auto captureProgram(const Counter<std::uint8_t>& tempo, const Counter<std::uint8_t>& swing,
                    const Counter<std::uint8_t>& volume, const Sequencer& sequencer,
                    const MidiClockSettings& midiClock) -> Program {
    Program program{
        .tempo = tempo.getValue(),
        .swing = swing.getValue(),
        .volume = volume.getValue(),
        .midiClockMode = midiClock.mode(),
    };
    const auto& steps = sequencer.steps();
    static_assert(PROGRAM_STEP_COUNT == STEPS_COUNT);
    for (std::size_t index = 0; index < PROGRAM_STEP_COUNT; ++index) {
        program.steps[index] = {
            .enabled = steps[index].isEnabled,
            .note = steps[index].note,
            .velocity = steps[index].velocity,
            .gate = steps[index].gate,
        };
    }
    return program;
}

auto applyProgram(const Program& program, Counter<std::uint8_t>& tempo,
                  Counter<std::uint8_t>& swing, Counter<std::uint8_t>& volume, Sequencer& sequencer,
                  MidiClockSettings& midiClock) -> bool {
    auto normalized = program;
    if (normalized.swing > PROGRAM_MAX_SWING && normalized.swing <= PROGRAM_LEGACY_MAX_SWING) {
        normalized.swing = PROGRAM_MAX_SWING;
    }
    if (!isValid(normalized)) {
        return false;
    }

    std::array<SequencerStep, STEPS_COUNT> steps{};
    static_assert(PROGRAM_STEP_COUNT == STEPS_COUNT);
    for (std::size_t index = 0; index < PROGRAM_STEP_COUNT; ++index) {
        steps[index] = {
            .isEnabled = normalized.steps[index].enabled,
            .note = normalized.steps[index].note,
            .velocity = normalized.steps[index].velocity,
            .gate = normalized.steps[index].gate,
        };
    }

    tempo.setValue(normalized.tempo);
    swing.setValue(normalized.swing);
    volume.setValue(normalized.volume);
    sequencer.setBpm(normalized.tempo);
    sequencer.setSwing(normalized.swing);
    sequencer.setSteps(steps);
    midiClock.apply(normalized.midiClockMode);
    return true;
}

} // namespace SwingMetro
