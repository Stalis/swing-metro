#include "test_sequencer.h"

#include "engine/sequencer.h"

#include <array>
#include <unity.h>

namespace {

using SwingMetro::MidiEventQueue;
using SwingMetro::MidiEventQueueEnqueueResult;
using SwingMetro::TransportPosition;

void enableStep(Sequencer& sequencer, StepIndex index, MIDI_Note note, uint8_t velocity) {
    auto steps = sequencer.steps();
    steps[index] = {true, note, velocity};
    sequencer.setSteps(steps);
}

void test_start_schedules_tick_zero_and_two_tick_horizon() {
    Sequencer sequencer;
    MidiEventQueue queue;
    enableStep(sequencer, 0, 60, 100);
    enableStep(sequencer, 1, 61, 101);

    sequencer.start();
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, sequencer.scheduleThrough({0, 0}, queue));
    TEST_ASSERT_EQUAL_UINT32(1, queue.size());
    TEST_ASSERT_EQUAL_UINT64(0, queue.nextPosition()->tick);
    TEST_ASSERT_EQUAL_UINT8(60, queue.drainAt({0, 0}).events[0].packet[2]);
    sequencer.notifyBoundaryReached(0);

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, sequencer.scheduleThrough({4, 0}, queue));
    const auto boundary = queue.drainAt({6, 0});
    TEST_ASSERT_EQUAL_UINT32(2, boundary.count);
    TEST_ASSERT_EQUAL_HEX8(0x80, boundary.events[0].packet[1]);
    TEST_ASSERT_EQUAL_UINT8(60, boundary.events[0].packet[2]);
    TEST_ASSERT_EQUAL_HEX8(0x90, boundary.events[1].packet[1]);
    TEST_ASSERT_EQUAL_UINT8(61, boundary.events[1].packet[2]);
}

void test_steps_wrap_after_sixteen_sixteenth_boundaries() {
    Sequencer sequencer;
    MidiEventQueue queue;
    enableStep(sequencer, 0, 60, 100);
    enableStep(sequencer, 15, 75, 100);
    sequencer.start();

    for (uint8_t step = 0; step < STEPS_COUNT; ++step) {
        const auto tick =
            static_cast<SwingMetro::TransportTick>(step) * SwingMetro::kTicksPerSixteenth;
        TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                          sequencer.scheduleThrough({tick, 0}, queue));
        (void)queue.drainAt({tick, 0});
        sequencer.notifyBoundaryReached(tick);
    }

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, sequencer.scheduleThrough({96, 0}, queue));
    const auto boundary = queue.drainAt({96, 0});
    TEST_ASSERT_EQUAL_UINT8(60, boundary.events[boundary.count - 1].packet[2]);
}

void test_queue_rejection_keeps_boundary_retryable() {
    Sequencer sequencer;
    MidiEventQueue queue;
    enableStep(sequencer, 0, 60, 100);
    for (std::size_t index = 0; index < MidiEventQueue::kCapacity; ++index) {
        TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({100 + index, 0}, {0x09, 0x90, 1, 1}));
    }

    sequencer.start();
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::CapacityExceeded,
                      sequencer.scheduleThrough({0, 0}, queue));
    queue.clear();
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, sequencer.scheduleThrough({0, 0}, queue));
    TEST_ASSERT_EQUAL_UINT8(60, queue.drainAt({0, 0}).events[0].packet[2]);
}

void test_stop_returns_actual_not_projected_note() {
    Sequencer sequencer;
    MidiEventQueue queue;
    enableStep(sequencer, 0, 60, 100);
    enableStep(sequencer, 1, 61, 100);
    sequencer.start();
    (void)sequencer.scheduleThrough({0, 0}, queue);
    (void)queue.drainAt({0, 0});
    sequencer.notifyBoundaryReached(0);
    (void)sequencer.scheduleThrough({4, 0}, queue);

    queue.clear();
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.stop());
    TEST_ASSERT_FALSE(sequencer.stop().has_value());
    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());
}

void test_continue_preserves_next_boundary() {
    Sequencer sequencer;
    MidiEventQueue queue;
    enableStep(sequencer, 0, 60, 100);
    enableStep(sequencer, 1, 61, 100);
    sequencer.start();
    (void)sequencer.scheduleThrough({0, 0}, queue);
    (void)queue.drainAt({0, 0});
    sequencer.notifyBoundaryReached(0);
    (void)sequencer.stop();
    sequencer.continuePlayback();

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, sequencer.scheduleThrough({4, 0}, queue));
    TEST_ASSERT_EQUAL_UINT8(61, queue.drainAt({6, 0}).events[0].packet[2]);
}

void test_legacy_timing_api_remains_compatible_until_stage_six() {
    Sequencer sequencer;
    sequencer.toggleRunning(1000);
    TEST_ASSERT_TRUE(sequencer.isRunning());
    TEST_ASSERT_TRUE(sequencer.update(126000));
    TEST_ASSERT_EQUAL_UINT8(0, sequencer.getCurrentStepIndex());
}

} // namespace

void test_sequencer_main() {
    RUN_TEST(test_start_schedules_tick_zero_and_two_tick_horizon);
    RUN_TEST(test_steps_wrap_after_sixteen_sixteenth_boundaries);
    RUN_TEST(test_queue_rejection_keeps_boundary_retryable);
    RUN_TEST(test_stop_returns_actual_not_projected_note);
    RUN_TEST(test_continue_preserves_next_boundary);
    RUN_TEST(test_legacy_timing_api_remains_compatible_until_stage_six);
}
