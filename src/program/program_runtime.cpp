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
        };
    }
    return program;
}

auto applyProgram(const Program& program, Counter<std::uint8_t>& tempo,
                  Counter<std::uint8_t>& swing, Counter<std::uint8_t>& volume, Sequencer& sequencer,
                  MidiClockSettings& midiClock) -> bool {
    if (!isValid(program)) {
        return false;
    }

    std::array<SequencerStep, STEPS_COUNT> steps{};
    static_assert(PROGRAM_STEP_COUNT == STEPS_COUNT);
    for (std::size_t index = 0; index < PROGRAM_STEP_COUNT; ++index) {
        steps[index] = {
            .isEnabled = program.steps[index].enabled,
            .note = program.steps[index].note,
            .velocity = program.steps[index].velocity,
        };
    }

    tempo.setValue(program.tempo);
    swing.setValue(program.swing);
    volume.setValue(program.volume);
    sequencer.setBpm(program.tempo);
    sequencer.setSteps(steps);
    midiClock.apply(program.midiClockMode);
    return true;
}

} // namespace SwingMetro
