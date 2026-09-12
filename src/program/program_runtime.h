#pragma once

#include "engine/sequencer.h"
#include "program/program.h"

#include <cstdint>
#include <utils/counter.h>

namespace SwingMetro {

[[nodiscard]] auto captureProgram(const Counter<std::uint8_t>& tempo,
                                  const Counter<std::uint8_t>& swing,
                                  const Counter<std::uint8_t>& volume, const Sequencer& sequencer,
                                  const MidiClockSettings& midiClock) -> Program;

[[nodiscard]] auto applyProgram(const Program& program, Counter<std::uint8_t>& tempo,
                                Counter<std::uint8_t>& swing, Counter<std::uint8_t>& volume,
                                Sequencer& sequencer, MidiClockSettings& midiClock) -> bool;

} // namespace SwingMetro
