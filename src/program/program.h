#pragma once

#include "engine/midi_clock_mode.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

constexpr std::size_t PROGRAM_STEP_COUNT = 16;
constexpr std::uint8_t PROGRAM_DEFAULT_TEMPO = 120;
constexpr std::uint8_t PROGRAM_MIN_TEMPO = 40;
constexpr std::uint8_t PROGRAM_MAX_TEMPO = 240;
constexpr std::uint8_t PROGRAM_DEFAULT_SWING = 50;
constexpr std::uint8_t PROGRAM_MIN_SWING = 50;
constexpr std::uint8_t PROGRAM_MAX_SWING = 100;
constexpr std::uint8_t PROGRAM_DEFAULT_VOLUME = 100;
constexpr std::uint8_t PROGRAM_MIN_VOLUME = 0;
constexpr std::uint8_t PROGRAM_MAX_VOLUME = 100;
constexpr std::uint8_t PROGRAM_DEFAULT_NOTE = 36;
constexpr std::uint8_t PROGRAM_MIN_NOTE = 36;
constexpr std::uint8_t PROGRAM_MAX_NOTE = 127;
constexpr std::uint8_t PROGRAM_DEFAULT_VELOCITY = 127;
constexpr std::uint8_t PROGRAM_MIN_VELOCITY = 1;
constexpr std::uint8_t PROGRAM_MAX_VELOCITY = 127;

struct ProgramStep {
    bool enabled = false;
    std::uint8_t note = PROGRAM_DEFAULT_NOTE;
    std::uint8_t velocity = PROGRAM_DEFAULT_VELOCITY;
};

struct Program {
    std::uint8_t tempo = PROGRAM_DEFAULT_TEMPO;
    std::uint8_t swing = PROGRAM_DEFAULT_SWING;
    std::uint8_t volume = PROGRAM_DEFAULT_VOLUME;
    MidiClockMode midiClockMode = MidiClockMode::Off;
    std::array<ProgramStep, PROGRAM_STEP_COUNT> steps{};
};

[[nodiscard]] constexpr auto isValidMidiClockMode(MidiClockMode mode) -> bool {
    return mode == MidiClockMode::Off || mode == MidiClockMode::Internal ||
           mode == MidiClockMode::External;
}

[[nodiscard]] constexpr auto isValid(const ProgramStep& step) -> bool {
    return step.note >= PROGRAM_MIN_NOTE && step.note <= PROGRAM_MAX_NOTE &&
           step.velocity >= PROGRAM_MIN_VELOCITY && step.velocity <= PROGRAM_MAX_VELOCITY;
}

[[nodiscard]] constexpr auto isValid(const Program& program) -> bool {
    if (program.tempo < PROGRAM_MIN_TEMPO || program.tempo > PROGRAM_MAX_TEMPO ||
        program.swing < PROGRAM_MIN_SWING || program.swing > PROGRAM_MAX_SWING ||
        program.volume < PROGRAM_MIN_VOLUME || program.volume > PROGRAM_MAX_VOLUME ||
        !isValidMidiClockMode(program.midiClockMode)) {
        return false;
    }

    for (const auto& step : program.steps) {
        if (!isValid(step)) {
            return false;
        }
    }
    return true;
}

} // namespace SwingMetro
