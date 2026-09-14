#include "test_transport_controller.h"

#include "engine/transport_controller.h"

#include <array>
#include <unity.h>

namespace {

class Sink final : public SwingMetro::MidiPacketSink {
  public:
    auto send(const SwingMetro::MidiUsbPacket& packet) -> void override {
        packets[count++] = packet;
    }

    std::array<SwingMetro::MidiUsbPacket, 16> packets{};
    std::size_t count = 0;
};

auto event(SwingMetro::MidiRealtimeEventType type, std::uint32_t timestampUs)
    -> SwingMetro::MidiRealtimeEvent {
    return {type, timestampUs};
}

void enableFirstStep(Sequencer& sequencer) { sequencer.toggleStep(0); }

void test_dispatcher_sends_tick_before_phase_zero_and_due_phase() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink};
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, SwingMetro::phaseFromPercent(50)}, {0x09, 0x90, 60, 100}));

    dispatcher.start(true);
    dispatcher.consumeTick({1'000, 20'000}, true);
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[1][1]);
    dispatcher.dispatchDue(11'000);
    TEST_ASSERT_EQUAL_UINT32(3, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[2][1]);
}

void test_internal_start_emits_start_tick_and_queued_note() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_TRUE(controller.usesInternalTiming());
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.packets[0][1]);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    controller.process(1'000, ticks);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[1][1]);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[2][1]);
}

void test_external_start_waits_for_measured_tick_and_never_echoes_clock() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::External);
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Start, 0));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 1'000));
    controller.process(1'000, ticks);
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 21'833));
    TEST_ASSERT_EQUAL_UINT32(1, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[0][1]);
}

void test_repeated_external_start_stops_sounding_note_before_reset() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};

    controller.applyMode(SwingMetro::MidiClockMode::External);
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Start, 0));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 1'000));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 21'833));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Start, 22'000));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 42'833));

    TEST_ASSERT_EQUAL_UINT32(3, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[1][1]);
    TEST_ASSERT_EQUAL_UINT8(36, sink.packets[1][2]);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[2][1]);
}

void test_stop_switch_and_loss_send_one_note_off_without_realtime_leak() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    controller.process(1'000, ticks);
    controller.applyMode(SwingMetro::MidiClockMode::External);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[3][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.packets[4][1]);
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Start, 30'000));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 31'000));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 51'833));
    controller.process(51'833 + SwingMetro::ExternalMidiClock::CLOCK_LOSS_TIMEOUT_US, ticks);
    TEST_ASSERT_EQUAL_UINT32(7, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[5][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[6][1]);
    TEST_ASSERT_FALSE(sequencer.isRunning());
}

void test_local_toggle_starts_then_stops_external_without_continue_packet() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};

    controller.applyMode(SwingMetro::MidiClockMode::External);
    controller.toggle(0);
    TEST_ASSERT_TRUE(sequencer.isRunning());
    controller.toggle(1);
    TEST_ASSERT_FALSE(sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void test_external_continue_waits_for_next_tick_without_retriggering() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};

    controller.applyMode(SwingMetro::MidiClockMode::External);
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Start, 0));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 1'000));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 21'833));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Stop, 22'000));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Continue, 23'000));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 42'666));
    TEST_ASSERT_TRUE(sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[1][1]);
}

} // namespace

void test_transport_controller_main() {
    RUN_TEST(test_dispatcher_sends_tick_before_phase_zero_and_due_phase);
    RUN_TEST(test_internal_start_emits_start_tick_and_queued_note);
    RUN_TEST(test_external_start_waits_for_measured_tick_and_never_echoes_clock);
    RUN_TEST(test_repeated_external_start_stops_sounding_note_before_reset);
    RUN_TEST(test_stop_switch_and_loss_send_one_note_off_without_realtime_leak);
    RUN_TEST(test_local_toggle_starts_then_stops_external_without_continue_packet);
    RUN_TEST(test_external_continue_waits_for_next_tick_without_retriggering);
}
