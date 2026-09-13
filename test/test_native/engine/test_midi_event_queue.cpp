#include "test_midi_event_queue.h"

#include "engine/midi_event_queue.h"

#include <cstddef>
#include <cstdint>
#include <unity.h>

namespace {

using SwingMetro::MidiEventQueue;
using SwingMetro::MidiEventQueueEnqueueResult;
using SwingMetro::MidiUsbPacket;
using SwingMetro::phaseFromPercent;
using SwingMetro::TransportPosition;

constexpr MidiUsbPacket kClock = {0x0F, 0xF8, 0x00, 0x00};
constexpr MidiUsbPacket kNoteOff = {0x08, 0x80, 0x3C, 0x40};
constexpr MidiUsbPacket kNoteOn = {0x09, 0x90, 0x3C, 0x40};
constexpr MidiUsbPacket kNoteOnZeroVelocity = {0x09, 0x90, 0x3D, 0x00};
constexpr MidiUsbPacket kOther = {0x0B, 0xB0, 0x01, 0x7F};

void test_tick_quota_rejects_ninth_packet_without_changing_queue() {
    MidiEventQueue queue;
    for (std::size_t index = 0; index < MidiEventQueue::kMaxPacketsPerTick; ++index) {
        TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({4, static_cast<std::uint16_t>(index)}, kNoteOn));
    }

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::TickQuotaExceeded,
                      queue.enqueue({4, 75}, kClock));
    TEST_ASSERT_EQUAL_UINT32(MidiEventQueue::kMaxPacketsPerTick, queue.size());
    TEST_ASSERT_FALSE(queue.empty());
}

void test_capacity_rejects_packet_without_changing_queue() {
    MidiEventQueue queue;
    for (std::size_t index = 0; index < MidiEventQueue::kCapacity; ++index) {
        TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({index, 0}, kNoteOn));
    }

    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::CapacityExceeded,
                      queue.enqueue({16, 0}, kClock));
    TEST_ASSERT_EQUAL_UINT32(MidiEventQueue::kCapacity, queue.size());
}

void test_drain_respects_phase_zero_fifty_and_seventy_five() {
    MidiEventQueue queue;
    constexpr auto phase50 = phaseFromPercent(50);
    constexpr auto phase75 = phaseFromPercent(75);
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({7, phase75}, kOther));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({7, 0}, kClock));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({7, phase50}, kNoteOn));

    const auto result = queue.drainAt({7, phase75});
    TEST_ASSERT_EQUAL_UINT32(3, result.count);
    TEST_ASSERT_EQUAL_UINT16(0, result.events[0].target.phase);
    TEST_ASSERT_EQUAL_UINT16(phase50, result.events[1].target.phase);
    TEST_ASSERT_EQUAL_UINT16(phase75, result.events[2].target.phase);
    TEST_ASSERT_TRUE(queue.empty());
}

void test_phase_zero_prioritizes_clock_note_off_note_on_and_other_stably() {
    MidiEventQueue queue;
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, kOther));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, kNoteOn));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, kNoteOnZeroVelocity));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, kNoteOff));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 0}, kClock));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({2, 0}, {0x09, 0x90, 0x3E, 0x41}));

    const auto result = queue.drainAt({2, 0});
    TEST_ASSERT_EQUAL_UINT32(6, result.count);
    TEST_ASSERT_EQUAL_HEX8(0xF8, result.events[0].packet[1]);
    TEST_ASSERT_EQUAL_HEX8(0x3D, result.events[1].packet[2]);
    TEST_ASSERT_EQUAL_HEX8(0x3C, result.events[2].packet[2]);
    TEST_ASSERT_EQUAL_HEX8(0x3C, result.events[3].packet[2]);
    TEST_ASSERT_EQUAL_HEX8(0x3E, result.events[4].packet[2]);
    TEST_ASSERT_EQUAL_HEX8(0xB0, result.events[5].packet[1]);
}

void test_nonzero_phase_preserves_insertion_order() {
    MidiEventQueue queue;
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 50}, kOther));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 50}, kClock));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 50}, kNoteOff));

    const auto result = queue.drainAt({2, 50});
    TEST_ASSERT_EQUAL_HEX8(0xB0, result.events[0].packet[1]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, result.events[1].packet[1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, result.events[2].packet[1]);
}

void test_drain_returns_late_events_and_preserves_all_packet_bytes() {
    MidiEventQueue queue;
    constexpr MidiUsbPacket packet = {0x09, 0x91, 0x45, 0x64};
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({2, 50}, packet));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({3, 0}, kClock));

    const auto result = queue.drainAt({3, 0});
    TEST_ASSERT_EQUAL_UINT32(2, result.count);
    TEST_ASSERT_EQUAL_UINT32(1, result.lateCount);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(packet.data(), result.events[0].packet.data(), packet.size());
    TEST_ASSERT_TRUE(queue.empty());
}

void test_clear_removes_all_events() {
    MidiEventQueue queue;
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({1, 0}, kNoteOn));
    queue.clear();

    TEST_ASSERT_TRUE(queue.empty());
    TEST_ASSERT_EQUAL_UINT32(0, queue.size());
    TEST_ASSERT_EQUAL_UINT32(0, queue.drainAt({1, 0}).count);
}

} // namespace

void test_midi_event_queue_main() {
    RUN_TEST(test_tick_quota_rejects_ninth_packet_without_changing_queue);
    RUN_TEST(test_capacity_rejects_packet_without_changing_queue);
    RUN_TEST(test_drain_respects_phase_zero_fifty_and_seventy_five);
    RUN_TEST(test_phase_zero_prioritizes_clock_note_off_note_on_and_other_stably);
    RUN_TEST(test_nonzero_phase_preserves_insertion_order);
    RUN_TEST(test_drain_returns_late_events_and_preserves_all_packet_bytes);
    RUN_TEST(test_clear_removes_all_events);
}
