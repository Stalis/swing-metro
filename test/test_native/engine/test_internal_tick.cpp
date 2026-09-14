#include "test_internal_tick.h"

#include "engine/internal_tick_consumer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unity.h>

namespace {

using SwingMetro::InternalTickConsumer;
using SwingMetro::InternalTickRecord;
using SwingMetro::InternalTickSource;
using SwingMetro::InternalTickStore;
using SwingMetro::MidiEventQueue;
using SwingMetro::MidiEventQueueEnqueueResult;
using SwingMetro::MidiUsbPacket;
using SwingMetro::phaseFromPercent;
using SwingMetro::Transport;

constexpr MidiUsbPacket NOTE_OFF = {0x08, 0x80, 0x3C, 0x40};
constexpr MidiUsbPacket NOTE_ON = {0x09, 0x90, 0x3C, 0x40};

struct PacketLog {
    std::array<MidiUsbPacket, 32> packets{};
    std::size_t count = 0;

    auto send(const MidiUsbPacket& packet) -> void { packets[count++] = packet; }
};

void test_interval_residual_is_distributed_at_40_120_and_240_bpm() {
    InternalTickSource source;
    TEST_ASSERT_EQUAL_UINT32(62500, source.start(0, 40));
    TEST_ASSERT_EQUAL_UINT32(62500, source.onAlarm(62500));

    TEST_ASSERT_EQUAL_UINT32(20833, source.start(0, 120));
    TEST_ASSERT_EQUAL_UINT32(20833, source.onAlarm(20833));
    TEST_ASSERT_EQUAL_UINT32(20834, source.onAlarm(41666));

    TEST_ASSERT_EQUAL_UINT32(10416, source.start(0, 240));
    TEST_ASSERT_EQUAL_UINT32(10417, source.onAlarm(10416));
    TEST_ASSERT_EQUAL_UINT32(10417, source.onAlarm(20833));
}

void test_phase_deadlines_for_zero_fifty_and_seventy_five_percent() {
    Transport transport;
    MidiEventQueue queue;
    InternalTickConsumer consumer{transport, queue};
    PacketLog log;
    consumer.start([&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({0, 0}, NOTE_OFF));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phaseFromPercent(50)}, NOTE_ON));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phaseFromPercent(75)}, NOTE_OFF));
    InternalTickStore<> ticks;
    TEST_ASSERT_TRUE(ticks.publish({1000, 20000}));

    consumer.consume(ticks, 1000, [&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL_HEX8(0xFA, log.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, log.packets[1][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, log.packets[2][1]);
    TEST_ASSERT_EQUAL_UINT32(11000, consumer.nextDeadlineUs());
    consumer.dispatchDue(11000, [&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL_HEX8(0x90, log.packets[3][1]);
    TEST_ASSERT_EQUAL_UINT32(16000, consumer.nextDeadlineUs());
}

void test_phase_zero_orders_clock_before_note_off_before_note_on() {
    Transport transport;
    MidiEventQueue queue;
    InternalTickConsumer consumer{transport, queue};
    PacketLog log;
    consumer.start([&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({0, 0}, NOTE_ON));
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({0, 0}, NOTE_OFF));
    InternalTickStore<> ticks;
    TEST_ASSERT_TRUE(ticks.publish({0, 1000}));

    consumer.consume(ticks, 0, [&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL_HEX8(0xF8, log.packets[1][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, log.packets[2][1]);
    TEST_ASSERT_EQUAL_HEX8(0x90, log.packets[3][1]);
}

void test_start_stop_and_continue_emit_realtime_messages() {
    Transport transport;
    MidiEventQueue queue;
    InternalTickConsumer consumer{transport, queue};
    PacketLog log;
    consumer.start([&](const MidiUsbPacket& packet) { log.send(packet); });
    consumer.stop([&](const MidiUsbPacket& packet) { log.send(packet); });
    consumer.continuePlayback([&](const MidiUsbPacket& packet) { log.send(packet); });

    TEST_ASSERT_EQUAL_HEX8(0xFA, log.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, log.packets[1][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFB, log.packets[2][1]);
    TEST_ASSERT_TRUE(transport.snapshot().running);
}

void test_bpm_change_recalculates_unsent_open_tick_deadline() {
    Transport transport;
    MidiEventQueue queue;
    InternalTickConsumer consumer{transport, queue};
    PacketLog log;
    consumer.start([&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phaseFromPercent(50)}, NOTE_ON));
    InternalTickStore<> ticks;
    TEST_ASSERT_TRUE(ticks.publish({1000, 20833}));
    consumer.consume(ticks, 1000, [&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL_UINT32(11416, consumer.nextDeadlineUs());

    consumer.setBpm(240);
    TEST_ASSERT_EQUAL_UINT32(6208, consumer.nextDeadlineUs());
}

void test_deadlines_work_across_micros_wraparound() {
    Transport transport;
    MidiEventQueue queue;
    InternalTickConsumer consumer{transport, queue};
    PacketLog log;
    consumer.start([&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phaseFromPercent(50)}, NOTE_ON));
    InternalTickStore<> ticks;
    TEST_ASSERT_TRUE(ticks.publish({0xFFFFFFE0U, 64}));
    consumer.consume(ticks, 0xFFFFFFE0U, [&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL_UINT32(0, consumer.nextDeadlineUs());
    consumer.dispatchDue(0, [&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL_HEX8(0x90, log.packets[2][1]);
}

void test_catch_up_is_bounded_and_discards_residual_ticks() {
    Transport transport;
    MidiEventQueue queue;
    InternalTickConsumer consumer{transport, queue};
    PacketLog log;
    consumer.start([&](const MidiUsbPacket& packet) { log.send(packet); });
    InternalTickStore<> ticks;
    for (std::uint32_t timestamp = 1; timestamp <= 5; ++timestamp) {
        TEST_ASSERT_TRUE(ticks.publish({timestamp, 1000}));
    }

    consumer.consume(ticks, 100, [&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL_UINT64(3, transport.position().tick);
    TEST_ASSERT_EQUAL_UINT32(4, consumer.diagnostics().lateTicks);
    TEST_ASSERT_EQUAL_UINT32(1, consumer.diagnostics().droppedTicks);
}

void test_future_queue_events_wait_for_their_tick() {
    Transport transport;
    MidiEventQueue queue;
    InternalTickConsumer consumer{transport, queue};
    PacketLog log;
    consumer.start([&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL(MidiEventQueueEnqueueResult::Ok, queue.enqueue({1, 0}, NOTE_ON));
    InternalTickStore<> ticks;
    TEST_ASSERT_TRUE(ticks.publish({0, 1000}));
    consumer.consume(ticks, 0, [&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL_UINT32(2, log.count);
    TEST_ASSERT_TRUE(ticks.publish({1000, 1000}));
    consumer.consume(ticks, 1000, [&](const MidiUsbPacket& packet) { log.send(packet); });
    TEST_ASSERT_EQUAL_HEX8(0xF8, log.packets[2][1]);
    TEST_ASSERT_EQUAL_HEX8(0x90, log.packets[3][1]);
}

} // namespace

void test_internal_tick_main() {
    RUN_TEST(test_interval_residual_is_distributed_at_40_120_and_240_bpm);
    RUN_TEST(test_phase_deadlines_for_zero_fifty_and_seventy_five_percent);
    RUN_TEST(test_phase_zero_orders_clock_before_note_off_before_note_on);
    RUN_TEST(test_start_stop_and_continue_emit_realtime_messages);
    RUN_TEST(test_bpm_change_recalculates_unsent_open_tick_deadline);
    RUN_TEST(test_deadlines_work_across_micros_wraparound);
    RUN_TEST(test_catch_up_is_bounded_and_discards_residual_ticks);
    RUN_TEST(test_future_queue_events_wait_for_their_tick);
}
