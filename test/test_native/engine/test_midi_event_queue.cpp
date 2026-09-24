#include "test_midi_event_queue.h"

#include "engine/midi_event_queue.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unity.h>

namespace {

using SwingMetro::MidiEventQueue;
using SwingMetro::MidiEventQueueEnqueueResult;
using SwingMetro::MidiEventRequest;
using SwingMetro::MidiMessage;
using SwingMetro::MidiMessageType;
using SwingMetro::phaseFromPercent;

constexpr auto noteOn(std::uint8_t note = 60, std::uint8_t velocity = 64, std::uint8_t channel = 0)
    -> MidiMessage {
    return *MidiMessage::noteOn(channel, note, velocity);
}

constexpr auto noteOff(std::uint8_t note = 60) -> MidiMessage {
    return *MidiMessage::noteOff(0, note);
}

void test_clock_reserves_one_tick_packet_slot() {
    MidiEventQueue queue;
    for (std::size_t index = 0; index < MidiEventQueue::MAX_PACKETS_PER_TICK - 1; ++index) {
        TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({4, static_cast<std::uint16_t>(index)}, noteOn()));
    }

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({4, 0}, MidiMessage::clock()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::TickQuotaExceeded,
                      queue.enqueue({4, 75}, noteOn()));
    TEST_ASSERT_EQUAL_UINT32(MidiEventQueue::MAX_PACKETS_PER_TICK, queue.size());
    TEST_ASSERT_FALSE(queue.empty());
}

void test_capacity_rejects_packet_without_changing_queue() {
    MidiEventQueue queue;
    for (std::size_t index = 0; index < MidiEventQueue::CAPACITY; ++index) {
        TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({index, 0}, noteOn()));
    }

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::CapacityExceeded,
                      queue.enqueue({16, 0}, MidiMessage::clock()));
    TEST_ASSERT_EQUAL_UINT32(MidiEventQueue::CAPACITY, queue.size());
}

void test_batch_rejects_capacity_without_inserting_any_packet() {
    MidiEventQueue queue;
    for (std::size_t index = 0; index < MidiEventQueue::CAPACITY - 1; ++index) {
        TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({index, 0}, noteOn()));
    }
    const std::array<MidiEventRequest, 2> batch = {
        MidiEventRequest{{17, 0}, noteOff()},
        MidiEventRequest{{17, 0}, noteOn()},
    };

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::CapacityExceeded, queue.enqueueBatch(batch, 2));
    TEST_ASSERT_EQUAL_UINT32(MidiEventQueue::CAPACITY - 1, queue.size());
}

void test_batch_rejects_tick_quota_without_inserting_any_packet() {
    MidiEventQueue queue;
    for (std::size_t index = 0; index < MidiEventQueue::MAX_PACKETS_PER_TICK - 2; ++index) {
        TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({4, 0}, noteOn()));
    }
    const std::array<MidiEventRequest, 2> batch = {
        MidiEventRequest{{4, 0}, noteOff()},
        MidiEventRequest{{4, 0}, noteOn()},
    };

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::TickQuotaExceeded, queue.enqueueBatch(batch, 2));
    TEST_ASSERT_EQUAL_UINT32(MidiEventQueue::MAX_PACKETS_PER_TICK - 2, queue.size());
    queue.clear();
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueueBatch(batch, 2));
    TEST_ASSERT_EQUAL_UINT32(2, queue.size());
}

void test_batch_accepts_note_off_note_on_in_dispatch_order() {
    MidiEventQueue queue;
    const std::array<MidiEventRequest, 2> batch = {
        MidiEventRequest{{4, 0}, noteOff()},
        MidiEventRequest{{4, 0}, noteOn()},
    };

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueueBatch(batch, 2));
    const auto result = queue.drainAt({4, 0});
    TEST_ASSERT_TRUE(result.events[0].message.isNoteOffEquivalent());
    TEST_ASSERT_TRUE(result.events[1].message.isNoteOn());
}

void test_enqueued_message_is_immutable_copy() {
    MidiEventQueue queue;
    auto message = noteOn();
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({4, 0}, message));
    message = noteOn(127);

    TEST_ASSERT_EQUAL_UINT8(60, queue.drainAt({4, 0}).events[0].message.note());
}

void test_drain_respects_phase_zero_fifty_and_seventy_five() {
    MidiEventQueue queue;
    constexpr auto phase50 = phaseFromPercent(50);
    constexpr auto phase75 = phaseFromPercent(75);
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({7, phase75}, MidiMessage::start()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({7, 0}, MidiMessage::clock()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({7, phase50}, noteOn()));

    const auto result = queue.drainAt({7, phase75});
    TEST_ASSERT_EQUAL_UINT32(3, result.count);
    TEST_ASSERT_EQUAL_UINT16(0, result.events[0].target.phase);
    TEST_ASSERT_EQUAL_UINT16(phase50, result.events[1].target.phase);
    TEST_ASSERT_EQUAL_UINT16(phase75, result.events[2].target.phase);
    TEST_ASSERT_TRUE(queue.empty());
}

void test_phase_zero_prioritizes_clock_note_off_note_on_and_other_stably() {
    MidiEventQueue queue;
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, MidiMessage::start()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, noteOn()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, noteOn(61, 0)));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, noteOff()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, MidiMessage::clock()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, noteOn(62, 65)));

    const auto result = queue.drainAt({2, 0});
    TEST_ASSERT_EQUAL_UINT32(6, result.count);
    TEST_ASSERT_TRUE(result.events[0].message.isClock());
    TEST_ASSERT_EQUAL_UINT8(61, result.events[1].message.note());
    TEST_ASSERT_EQUAL_UINT8(60, result.events[2].message.note());
    TEST_ASSERT_EQUAL_UINT8(60, result.events[3].message.note());
    TEST_ASSERT_EQUAL_UINT8(62, result.events[4].message.note());
    TEST_ASSERT_EQUAL(MidiMessageType::Start, result.events[5].message.type());
}

void test_nonzero_phase_prioritizes_clock_note_off_note_on_and_other_stably() {
    MidiEventQueue queue;
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({2, 50}, MidiMessage::start()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({2, 50}, MidiMessage::clock()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 50}, noteOff()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 50}, noteOn()));

    const auto result = queue.drainAt({2, 50});
    TEST_ASSERT_TRUE(result.events[0].message.isClock());
    TEST_ASSERT_EQUAL(MidiMessageType::NoteOff, result.events[1].message.type());
    TEST_ASSERT_EQUAL(MidiMessageType::NoteOn, result.events[2].message.type());
    TEST_ASSERT_EQUAL(MidiMessageType::Start, result.events[3].message.type());
}

void test_batch_preserves_shared_launch_identity() {
    MidiEventQueue queue;
    constexpr SwingMetro::MidiLaunchId launchId = 42;
    const std::array<MidiEventRequest, 2> batch = {
        MidiEventRequest{{2, 50}, noteOn(), launchId},
        MidiEventRequest{{8, 50}, noteOff(), launchId},
    };

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueueBatch(batch, batch.size()));
    TEST_ASSERT_EQUAL_UINT32(launchId, queue.drainAt({2, 50}).events[0].launchId);
    TEST_ASSERT_EQUAL_UINT32(launchId, queue.drainAt({8, 50}).events[0].launchId);
}

void test_drain_returns_late_events_and_preserves_message() {
    MidiEventQueue queue;
    constexpr auto message = noteOn(69, 100, 1);
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 50}, message));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({3, 0}, MidiMessage::clock()));

    const auto result = queue.drainAt({3, 0});
    TEST_ASSERT_EQUAL_UINT32(2, result.count);
    TEST_ASSERT_EQUAL_UINT32(1, result.lateCount);
    TEST_ASSERT_EQUAL_UINT8(message.channel(), result.events[0].message.channel());
    TEST_ASSERT_EQUAL_UINT8(message.note(), result.events[0].message.note());
    TEST_ASSERT_EQUAL_UINT8(message.velocity(), result.events[0].message.velocity());
    TEST_ASSERT_TRUE(queue.empty());
}

void test_clear_removes_all_events() {
    MidiEventQueue queue;
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({1, 0}, noteOn()));
    queue.clear();

    TEST_ASSERT_TRUE(queue.empty());
    TEST_ASSERT_EQUAL_UINT32(0, queue.size());
    TEST_ASSERT_EQUAL_UINT32(0, queue.drainAt({1, 0}).count);
}

void test_next_position_is_read_only_and_returns_the_earliest_event() {
    MidiEventQueue queue;
    TEST_ASSERT_FALSE(queue.nextPosition().has_value());
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({3, 50}, noteOn()));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({2, 75}, MidiMessage::start()));

    const auto next = queue.nextPosition();
    TEST_ASSERT_TRUE(next.has_value());
    TEST_ASSERT_EQUAL_UINT64(2, next->tick);
    TEST_ASSERT_EQUAL_UINT16(75, next->phase);
    TEST_ASSERT_EQUAL_UINT32(2, queue.size());
}

void test_default_slots_are_gated_by_count() {
    MidiEventQueue queue;
    TEST_ASSERT_EQUAL_UINT32(0, queue.drainAt({0, 0}).count);
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({7, phaseFromPercent(50)}, noteOn()));
    const auto result = queue.drainAt({7, phaseFromPercent(50)});
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
    TEST_ASSERT_EQUAL_UINT8(60, result.events[0].message.note());
}

void test_front_and_pop_remove_only_the_scheduled_head() {
    MidiEventQueue queue;
    TEST_ASSERT_FALSE(queue.front().has_value());
    queue.popFront();
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, noteOn(62)));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({1, 0}, noteOn(61)));

    const auto first = queue.front();
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_EQUAL_UINT64(1, first->target.tick);
    TEST_ASSERT_EQUAL_UINT8(61, first->message.note());
    queue.popFront();

    TEST_ASSERT_EQUAL_UINT32(1, queue.size());
    TEST_ASSERT_EQUAL_UINT64(2, queue.front()->target.tick);
    TEST_ASSERT_EQUAL_UINT8(62, queue.front()->message.note());
}

} // namespace

void test_midi_event_queue_main() {
    RUN_TEST(test_clock_reserves_one_tick_packet_slot);
    RUN_TEST(test_capacity_rejects_packet_without_changing_queue);
    RUN_TEST(test_batch_rejects_capacity_without_inserting_any_packet);
    RUN_TEST(test_batch_rejects_tick_quota_without_inserting_any_packet);
    RUN_TEST(test_batch_accepts_note_off_note_on_in_dispatch_order);
    RUN_TEST(test_enqueued_message_is_immutable_copy);
    RUN_TEST(test_drain_respects_phase_zero_fifty_and_seventy_five);
    RUN_TEST(test_phase_zero_prioritizes_clock_note_off_note_on_and_other_stably);
    RUN_TEST(test_nonzero_phase_prioritizes_clock_note_off_note_on_and_other_stably);
    RUN_TEST(test_batch_preserves_shared_launch_identity);
    RUN_TEST(test_drain_returns_late_events_and_preserves_message);
    RUN_TEST(test_clear_removes_all_events);
    RUN_TEST(test_next_position_is_read_only_and_returns_the_earliest_event);
    RUN_TEST(test_default_slots_are_gated_by_count);
    RUN_TEST(test_front_and_pop_remove_only_the_scheduled_head);
}
