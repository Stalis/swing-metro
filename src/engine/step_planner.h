#pragma once

#include <array>
#include <cstdint>

#include "sequencer_step.h"

constexpr SwingMetro::TransportTick SCHEDULING_LOOKAHEAD_TICKS = 2;

class StepPlanner {
  public:
    void reset() noexcept;
    [[nodiscard]] SwingMetro::MidiEventQueueEnqueueResult
    scheduleThrough(SwingMetro::TransportPosition position,
                    const std::array<SequencerStep, STEPS_COUNT>& steps, uint8_t swing,
                    std::uint32_t sessionGeneration, SwingMetro::MidiEventQueue& queue);

  private:
    SwingMetro::TransportTick _nextBoundaryTick = 0;
    SwingMetro::MidiLaunchId _nextLaunchId = 1;
    bool _schedulingComplete = false;
};
