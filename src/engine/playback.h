#pragma once

#include "engine/sequencer.h"
#include "program/program.h"
#include "program/program_id.h"

#include <optional>

namespace SwingMetro {

class Playback {
  public:
    [[nodiscard]] auto sequencer() noexcept -> Sequencer& { return _sequencer; }
    [[nodiscard]] auto sequencer() const noexcept -> const Sequencer& { return _sequencer; }
    [[nodiscard]] auto selectedProgramId() const noexcept -> std::optional<ProgramId> {
        return _selectedProgramId;
    }
    [[nodiscard]] auto appliedProgram() const noexcept -> const Program& { return _appliedProgram; }

    auto applyProgram(const Program& program, std::optional<ProgramId> selectedProgramId) -> void;
    [[nodiscard]] auto scheduleThrough(TransportPosition position, MidiEventQueue& queue)
        -> MidiEventQueueEnqueueResult;
    auto refreshAppliedProgram(const Program& program) noexcept -> void;
    auto selectProgram(std::optional<ProgramId> selectedProgramId) noexcept -> void;

  private:
    Sequencer _sequencer;
    Program _appliedProgram{};
    std::optional<ProgramId> _selectedProgramId;
};

} // namespace SwingMetro
