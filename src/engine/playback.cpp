#include "engine/playback.h"

namespace SwingMetro {

auto Playback::applyProgram(const Program& program, std::optional<ProgramId> selectedProgramId)
    -> void {
    std::array<SequencerStep, STEPS_COUNT> steps{};
    static_assert(PROGRAM_STEP_COUNT == STEPS_COUNT);
    for (std::size_t index = 0; index < PROGRAM_STEP_COUNT; ++index) {
        steps[index] = {
            .isEnabled = program.steps[index].enabled,
            .note = program.steps[index].note,
            .velocity = program.steps[index].velocity,
            .gate = program.steps[index].gate,
        };
    }
    _sequencer.setSwing(program.swing);
    _sequencer.setSteps(steps);
    _selectedProgramId = selectedProgramId;
}

auto Playback::scheduleThrough(TransportPosition position, MidiEventQueue& queue)
    -> MidiEventQueueEnqueueResult {
    return _sequencer.scheduleThrough(position, queue);
}

auto Playback::selectProgram(std::optional<ProgramId> selectedProgramId) noexcept -> void {
    _selectedProgramId = selectedProgramId;
}

} // namespace SwingMetro
