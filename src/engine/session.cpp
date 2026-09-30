#include "engine/session.h"

namespace SwingMetro {

Session::Session(MidiMessageSink& sink) noexcept
    : _tempo({.step = 1,
              .value = PROGRAM_DEFAULT_TEMPO,
              .minValue = PROGRAM_MIN_TEMPO,
              .maxValue = PROGRAM_MAX_TEMPO,
              .overflowBehavior = CounterOverflowBehavior::Clamp}),
      _transport(_playback, _midiClock, sink) {}

auto Session::applyProgram(const Program& program, std::optional<ProgramId> selectedProgramId)
    -> bool {
    if (isRunning()) {
        return false;
    }

    auto normalized = program;
    if (normalized.swing > PROGRAM_MAX_SWING && normalized.swing <= PROGRAM_LEGACY_MAX_SWING) {
        normalized.swing = PROGRAM_MAX_SWING;
    }
    if (!isValid(normalized)) {
        return false;
    }

    _tempo.setValue(normalized.tempo);
    _playback.sequencer().setBpm(normalized.tempo);
    _midiClock.apply(normalized.midiClockMode);
    _playback.applyProgram(normalized, selectedProgramId);
    return true;
}

} // namespace SwingMetro
