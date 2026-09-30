#pragma once

#include "engine/playback.h"
#include "engine/transport_controller.h"

#include <utils/counter.h>

namespace SwingMetro {

class Session {
  public:
    explicit Session(MidiMessageSink& sink) noexcept;

    [[nodiscard]] auto playback() noexcept -> Playback& { return _playback; }
    [[nodiscard]] auto playback() const noexcept -> const Playback& { return _playback; }
    [[nodiscard]] auto tempo() noexcept -> Counter<std::uint8_t>& { return _tempo; }
    [[nodiscard]] auto midiClock() noexcept -> MidiClockSettings& { return _midiClock; }
    [[nodiscard]] auto transport() noexcept -> TransportController& { return _transport; }
    [[nodiscard]] auto isRunning() const noexcept -> bool {
        return _transport.isRunning() || _playback.sequencer().isRunning();
    }
    [[nodiscard]] auto generation() const noexcept -> std::uint32_t {
        return _transport.sessionGeneration();
    }

    [[nodiscard]] auto applyProgram(const Program& program,
                                    std::optional<ProgramId> selectedProgramId) -> bool;

  private:
    Playback _playback;
    Counter<std::uint8_t> _tempo;
    MidiClockSettings _midiClock;
    TransportController _transport;
};

} // namespace SwingMetro
