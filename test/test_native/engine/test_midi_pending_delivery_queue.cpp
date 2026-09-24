#include "test_midi_pending_delivery_queue.h"

#include "engine/midi_pending_delivery_queue.h"

#include <unity.h>

namespace {

constexpr auto event(std::size_t sequence, std::uint8_t note) -> SwingMetro::MidiEvent {
    return {{sequence, 0}, *SwingMetro::MidiMessage::noteOn(0, note, 100), sequence};
}

void test_pending_queue_preserves_fifo_identity_and_metadata() {
    SwingMetro::MidiPendingDeliveryQueue queue;
    TEST_ASSERT_TRUE(
        queue.push(event(7, 60), 1'234, 42, true, SwingMetro::MidiAttemptLateness::ClockAttempt));

    const auto front = queue.front();
    TEST_ASSERT_TRUE(front.has_value());
    TEST_ASSERT_EQUAL_UINT64(7, front->event.target.tick);
    TEST_ASSERT_EQUAL_UINT8(60, front->event.message.note());
    TEST_ASSERT_EQUAL_UINT32(1'234, front->deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(42, front->deliverySequenceNumber);
    TEST_ASSERT_TRUE(front->countsAsInternalClockAttempt);
    TEST_ASSERT_EQUAL(SwingMetro::MidiAttemptLateness::ClockAttempt, front->lateness);
}

void test_pending_queue_full_rejection_does_not_mutate_fifo() {
    SwingMetro::MidiPendingDeliveryQueue queue;
    for (std::size_t index = 0; index < SwingMetro::MidiPendingDeliveryQueue::CAPACITY; ++index) {
        TEST_ASSERT_TRUE(queue.push(event(index, static_cast<std::uint8_t>(index)),
                                    static_cast<std::uint32_t>(index), index));
    }
    TEST_ASSERT_TRUE(queue.full());
    TEST_ASSERT_FALSE(queue.push(event(99, 99), 99, 99));
    TEST_ASSERT_EQUAL_UINT32(SwingMetro::MidiPendingDeliveryQueue::CAPACITY, queue.size());
    TEST_ASSERT_EQUAL_UINT32(0, queue.front()->deliverySequenceNumber);

    for (std::size_t index = 0; index < SwingMetro::MidiPendingDeliveryQueue::CAPACITY; ++index) {
        TEST_ASSERT_EQUAL_UINT32(index, queue.front()->deliverySequenceNumber);
        queue.popFront();
    }
    TEST_ASSERT_TRUE(queue.empty());
}

void test_pending_queue_wraps_and_clear_resets_storage() {
    SwingMetro::MidiPendingDeliveryQueue queue;
    for (std::size_t index = 0; index < SwingMetro::MidiPendingDeliveryQueue::CAPACITY; ++index) {
        TEST_ASSERT_TRUE(queue.push(event(index, 60), 0, index));
    }
    queue.popFront();
    TEST_ASSERT_TRUE(queue.push(event(100, 61), 100, 100));
    for (std::size_t index = 1; index < SwingMetro::MidiPendingDeliveryQueue::CAPACITY; ++index) {
        TEST_ASSERT_EQUAL_UINT32(index, queue.front()->deliverySequenceNumber);
        queue.popFront();
    }
    TEST_ASSERT_EQUAL_UINT32(100, queue.front()->deliverySequenceNumber);
    queue.clear();
    TEST_ASSERT_TRUE(queue.empty());
    TEST_ASSERT_FALSE(queue.front().has_value());
}

void test_pending_queue_reserves_terminal_slots_and_filters_stably() {
    SwingMetro::MidiPendingDeliveryQueue queue;
    for (std::size_t index = 0; index < SwingMetro::MidiPendingDeliveryQueue::NORMAL_CAPACITY;
         ++index) {
        TEST_ASSERT_TRUE(
            queue.pushNormal(event(index, static_cast<std::uint8_t>(index)), 0, index));
    }
    TEST_ASSERT_FALSE(queue.pushNormal(event(20, 100), 0, 20));
    TEST_ASSERT_TRUE(
        queue.push(event(21, 101), 0, 21, false, SwingMetro::MidiAttemptLateness::None, 1, true));
    TEST_ASSERT_TRUE(
        queue.push(event(22, 102), 0, 22, false, SwingMetro::MidiAttemptLateness::None, 1, true));

    const auto removed = queue.removeIf([](const SwingMetro::PendingMidiEvent& pending) {
        return pending.event.message.note() == 1 || pending.event.message.note() == 3;
    });
    TEST_ASSERT_EQUAL_UINT32(2, removed.noteOns);
    TEST_ASSERT_EQUAL_UINT8(0, queue.front()->event.message.note());
    queue.popFront();
    TEST_ASSERT_EQUAL_UINT8(2, queue.front()->event.message.note());
}

void test_pending_retry_ordinal_and_terminal_summary_are_bounded() {
    SwingMetro::MidiPendingDeliveryQueue queue;
    TEST_ASSERT_TRUE(queue.push(event(0, 60), 0, 0));
    for (std::size_t attempt = 0; attempt < 300; ++attempt) {
        queue.markFrontRetry();
    }
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, queue.front()->attemptOrdinal);
    TEST_ASSERT_TRUE(queue.front()->retrySeen);

    queue.clear();
    const SwingMetro::MidiEvent noteOff{{0, 0}, *SwingMetro::MidiMessage::noteOff(0, 60), 0};
    const SwingMetro::MidiEvent stop{{0, 0}, SwingMetro::MidiMessage::stop(), 1};
    TEST_ASSERT_TRUE(
        queue.push(noteOff, 0, 0, false, SwingMetro::MidiAttemptLateness::None, 1, true));
    TEST_ASSERT_TRUE(queue.push(stop, 0, 1, false, SwingMetro::MidiAttemptLateness::None, 1, true));
    const auto removed = queue.removeIf([](const SwingMetro::PendingMidiEvent&) { return true; });
    TEST_ASSERT_EQUAL_UINT32(1, removed.noteOffs);
    TEST_ASSERT_EQUAL_UINT32(1, removed.stops);
    TEST_ASSERT_EQUAL_UINT32(1, removed.terminalNoteOffs);
    TEST_ASSERT_EQUAL_UINT32(1, removed.terminalStops);
}

void test_pending_note_off_lookup_uses_launch_identity() {
    SwingMetro::MidiPendingDeliveryQueue queue;
    const SwingMetro::MidiEvent oldOff{{0, 0}, *SwingMetro::MidiMessage::noteOff(0, 60), 0, 1, 2};
    TEST_ASSERT_TRUE(queue.push(oldOff, 0, 0, false, SwingMetro::MidiAttemptLateness::None, 2));
    TEST_ASSERT_TRUE(queue.hasNoteOff(2, 1));
    TEST_ASSERT_FALSE(queue.hasNoteOff(2, 2));
}

} // namespace

void test_midi_pending_delivery_queue_main() {
    RUN_TEST(test_pending_queue_preserves_fifo_identity_and_metadata);
    RUN_TEST(test_pending_queue_full_rejection_does_not_mutate_fifo);
    RUN_TEST(test_pending_queue_wraps_and_clear_resets_storage);
    RUN_TEST(test_pending_queue_reserves_terminal_slots_and_filters_stably);
    RUN_TEST(test_pending_retry_ordinal_and_terminal_summary_are_bounded);
    RUN_TEST(test_pending_note_off_lookup_uses_launch_identity);
}
