#include "test_sequencer.h"

#include "engine/sequencer.h"

#include <unity.h>

namespace {

using SwingMetro::MidiEventQueue;
using SwingMetro::MidiEventQueueEnqueueResult;

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
    const auto first = queue.drainAt({0, 0});
    TEST_ASSERT_TRUE(first.events[0].message.isNoteOn());
    TEST_ASSERT_EQUAL_UINT8(60, first.events[0].message.note());
    sequencer.notifyNoteOnAccepted(60);

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, sequencer.scheduleThrough({4, 0}, queue));
    const auto boundary = queue.drainAt({6, 0});
    TEST_ASSERT_EQUAL_UINT32(2, boundary.count);
    TEST_ASSERT_TRUE(boundary.events[0].message.isNoteOffEquivalent());
    TEST_ASSERT_EQUAL_UINT8(60, boundary.events[0].message.note());
    TEST_ASSERT_TRUE(boundary.events[1].message.isNoteOn());
    TEST_ASSERT_EQUAL_UINT8(61, boundary.events[1].message.note());
}

void test_steps_wrap_after_sixteen_sixteenth_boundaries() {
    Sequencer sequencer;
    MidiEventQueue queue;
    enableStep(sequencer, 0, 60, 100);
    enableStep(sequencer, 15, 75, 100);
    sequencer.start();

    for (uint8_t step = 0; step < STEPS_COUNT; ++step) {
        const auto tick =
            static_cast<SwingMetro::TransportTick>(step) * SwingMetro::TICKS_PER_SIXTEENTH;
        TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                          sequencer.scheduleThrough({tick, 0}, queue));
        (void)queue.drainAt({tick, 0});
    }

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, sequencer.scheduleThrough({96, 0}, queue));
    const auto boundary = queue.drainAt({96, 0});
    TEST_ASSERT_EQUAL_UINT8(60, boundary.events[boundary.count - 1].message.note());
}

void test_queue_rejection_keeps_boundary_retryable() {
    Sequencer sequencer;
    MidiEventQueue queue;
    enableStep(sequencer, 0, 60, 100);
    for (std::size_t index = 0; index < MidiEventQueue::CAPACITY; ++index) {
        TEST_ASSERT_EQUAL(
            MidiEventQueueEnqueueResult::Ok,
            queue.enqueue({100 + index, 0}, *SwingMetro::MidiMessage::noteOn(0, 1, 1)));
    }

    sequencer.start();
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::CapacityExceeded,
                      sequencer.scheduleThrough({0, 0}, queue));
    queue.clear();
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, sequencer.scheduleThrough({0, 0}, queue));
    TEST_ASSERT_EQUAL_UINT8(60, queue.drainAt({0, 0}).events[0].message.note());
}

void test_stop_returns_actual_not_projected_note() {
    Sequencer sequencer;
    MidiEventQueue queue;
    enableStep(sequencer, 0, 60, 100);
    enableStep(sequencer, 1, 61, 100);
    sequencer.start();
    (void)sequencer.scheduleThrough({0, 0}, queue);
    (void)queue.drainAt({0, 0});
    sequencer.notifyNoteOnAccepted(60);
    (void)sequencer.scheduleThrough({4, 0}, queue);

    queue.clear();
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.stop());
    TEST_ASSERT_FALSE(sequencer.stop().has_value());
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());
    sequencer.notifyNoteOffAccepted(60);
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
    sequencer.notifyNoteOnAccepted(60);
    (void)sequencer.stop();
    sequencer.continuePlayback();

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, sequencer.scheduleThrough({4, 0}, queue));
    TEST_ASSERT_EQUAL_UINT8(61, queue.drainAt({6, 0}).events[0].message.note());
}

void test_legacy_timing_api_remains_compatible_until_stage_six() {
    Sequencer sequencer;
    sequencer.toggleRunning(1000);
    TEST_ASSERT_TRUE(sequencer.isRunning());
    TEST_ASSERT_TRUE(sequencer.update(126000));
    TEST_ASSERT_EQUAL_UINT8(0, sequencer.getCurrentStepIndex());
}

void test_swing_phase_is_literal_and_only_delays_odd_steps() {
    TEST_ASSERT_EQUAL_UINT16(0, SwingMetro::swingPhase(0, 75));
    TEST_ASSERT_EQUAL_UINT16(0, SwingMetro::swingPhase(1, 50));
    TEST_ASSERT_EQUAL_UINT16(SwingMetro::phaseFromPercent(51), SwingMetro::swingPhase(1, 51));
    TEST_ASSERT_EQUAL_UINT16(SwingMetro::phaseFromPercent(75), SwingMetro::swingPhase(1, 75));
    TEST_ASSERT_EQUAL_UINT16(SwingMetro::phaseFromPercent(90), SwingMetro::swingPhase(15, 90));
    TEST_ASSERT_EQUAL_UINT16(0, SwingMetro::swingPhase(14, 90));
    TEST_ASSERT_EQUAL_UINT16(SwingMetro::phaseFromPercent(90), SwingMetro::swingPhase(1, 100));
}

void test_swing_schedules_only_odd_note_ons_at_a_phase() {
    Sequencer sequencer;
    MidiEventQueue queue;
    enableStep(sequencer, 0, 60, 100);
    enableStep(sequencer, 1, 61, 100);
    enableStep(sequencer, 2, 62, 100);
    sequencer.setSwing(75);
    sequencer.start();

    (void)sequencer.scheduleThrough({4, 0}, queue);
    const auto first = queue.drainAt({0, 0});
    TEST_ASSERT_EQUAL_UINT32(1, first.count);
    TEST_ASSERT_TRUE(first.events[0].message.isNoteOn());
    const auto boundary = queue.drainAt({6, 0});
    TEST_ASSERT_EQUAL_UINT32(1, boundary.count);
    TEST_ASSERT_TRUE(boundary.events[0].message.isNoteOffEquivalent());
    const auto delayed = queue.drainAt({6, SwingMetro::phaseFromPercent(75)});
    TEST_ASSERT_EQUAL_UINT32(1, delayed.count);
    TEST_ASSERT_TRUE(delayed.events[0].message.isNoteOn());

    (void)sequencer.scheduleThrough({10, 0}, queue);
    const auto next = queue.drainAt({12, 0});
    TEST_ASSERT_EQUAL_UINT32(2, next.count);
    TEST_ASSERT_TRUE(next.events[0].message.isNoteOffEquivalent());
    TEST_ASSERT_TRUE(next.events[1].message.isNoteOn());
}

void test_actual_sounding_state_follows_transmitted_messages() {
    Sequencer sequencer;

    sequencer.notifyNoteOnAccepted(60);
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());
    sequencer.notifyNoteOffAccepted(61);
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());
    sequencer.notifyNoteOffAccepted(60);
    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());
}

void test_cancelled_note_off_request_preserves_accepted_state_and_can_be_requested_again() {
    Sequencer sequencer;
    sequencer.notifyNoteOnAccepted(60);
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.stop());
    TEST_ASSERT_FALSE(sequencer.stop().has_value());

    sequencer.cancelRequestedNoteOff();
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.stop());
}

void test_disconnect_abandonment_is_not_note_off_acceptance() {
    Sequencer sequencer;
    sequencer.notifyNoteOnAccepted(60);
    sequencer.abandonRemoteNoteState();
    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(RemoteNoteState::Unknown),
                      static_cast<std::uint8_t>(sequencer.remoteNoteState()));
    sequencer.beginCleanRemoteSession();
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(RemoteNoteState::Clean),
                      static_cast<std::uint8_t>(sequencer.remoteNoteState()));
}

} // namespace

void test_sequencer_main() {
    RUN_TEST(test_start_schedules_tick_zero_and_two_tick_horizon);
    RUN_TEST(test_steps_wrap_after_sixteen_sixteenth_boundaries);
    RUN_TEST(test_queue_rejection_keeps_boundary_retryable);
    RUN_TEST(test_stop_returns_actual_not_projected_note);
    RUN_TEST(test_continue_preserves_next_boundary);
    RUN_TEST(test_legacy_timing_api_remains_compatible_until_stage_six);
    RUN_TEST(test_swing_phase_is_literal_and_only_delays_odd_steps);
    RUN_TEST(test_swing_schedules_only_odd_note_ons_at_a_phase);
    RUN_TEST(test_actual_sounding_state_follows_transmitted_messages);
    RUN_TEST(test_cancelled_note_off_request_preserves_accepted_state_and_can_be_requested_again);
    RUN_TEST(test_disconnect_abandonment_is_not_note_off_acceptance);
}
