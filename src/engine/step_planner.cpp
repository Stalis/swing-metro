#include "step_planner.h"

#include <limits>

void StepPlanner::reset() noexcept {
    _nextBoundaryTick = 0;
    _nextLaunchId = 1;
    _schedulingComplete = false;
}

SwingMetro::MidiEventQueueEnqueueResult
StepPlanner::scheduleThrough(SwingMetro::TransportPosition position,
                             const std::array<SequencerStep, STEPS_COUNT>& steps, uint8_t swing,
                             std::uint32_t sessionGeneration, SwingMetro::MidiEventQueue& queue) {
    if (_schedulingComplete) {
        return SwingMetro::MidiEventQueueEnqueueResult::Ok;
    }

    const auto horizon = position.tick > std::numeric_limits<SwingMetro::TransportTick>::max() -
                                             SCHEDULING_LOOKAHEAD_TICKS
                             ? std::numeric_limits<SwingMetro::TransportTick>::max()
                             : position.tick + SCHEDULING_LOOKAHEAD_TICKS;
    while (_nextBoundaryTick <= horizon) {
        const auto stepIndex = static_cast<StepIndex>(
            (_nextBoundaryTick / SwingMetro::TICKS_PER_SIXTEENTH) % STEPS_COUNT);
        const auto& step = steps[stepIndex];
        if (step.isEnabled) {
            const SwingMetro::TransportPosition onPosition{
                _nextBoundaryTick, SwingMetro::swingPhase(stepIndex, swing)};
            const auto launchId = _nextLaunchId;
            const auto offPosition = SwingMetro::gateDeadline(onPosition, step.gate);
            const std::array<SwingMetro::MidiEventRequest, 2> requests = {
                SwingMetro::MidiEventRequest{
                    onPosition, *SwingMetro::MidiMessage::noteOn(0, step.note, step.velocity),
                    launchId, sessionGeneration, offPosition},
                SwingMetro::MidiEventRequest{offPosition,
                                             *SwingMetro::MidiMessage::noteOff(0, step.note),
                                             launchId, sessionGeneration, offPosition},
            };
            const auto result = queue.enqueueBatch(requests, requests.size());
            if (result != SwingMetro::MidiEventQueueEnqueueResult::Ok) {
                return result;
            }
            ++_nextLaunchId;
            if (_nextLaunchId == 0) {
                ++_nextLaunchId;
            }
        }
        if (_nextBoundaryTick > std::numeric_limits<SwingMetro::TransportTick>::max() -
                                    SwingMetro::TICKS_PER_SIXTEENTH) {
            _schedulingComplete = true;
            break;
        }
        _nextBoundaryTick += SwingMetro::TICKS_PER_SIXTEENTH;
    }
    return SwingMetro::MidiEventQueueEnqueueResult::Ok;
}
