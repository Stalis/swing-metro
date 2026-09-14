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

void test_external_loss_relocks_with_one_clock_before_continue() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::External);
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Start, 0));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 1'000));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 21'833));
    controller.process(21'833 + SwingMetro::ExternalMidiClock::CLOCK_LOSS_TIMEOUT_US, ticks);
    TEST_ASSERT_FALSE(sequencer.isRunning());

    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 300'000));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<std::uint8_t>(controller.externalStatus()));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Continue, 300'001));
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 320'833));
    controller.process(320'833, ticks);

    TEST_ASSERT_TRUE(sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[1][1]);
}

void test_display_step_changes_at_boundary_not_while_scheduling() {
    Sequencer sequencer;
    auto steps = sequencer.steps();
    steps[0] = {true, 60, 100};
    steps[1] = {true, 61, 100};
    sequencer.setSteps(steps);
    sequencer.setSwing(75);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_FALSE(sequencer.getDisplayStepIndex().has_value());
    for (std::uint32_t tick = 0; tick <= 5; ++tick) {
        TEST_ASSERT_TRUE(ticks.publish({1'000 + tick * 20'000, 20'000}));
        controller.process(1'000 + tick * 20'000, ticks);
    }
    TEST_ASSERT_EQUAL_UINT8(0, *sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());

    TEST_ASSERT_TRUE(ticks.publish({121'000, 20'000}));
    controller.process(121'000, ticks);
    TEST_ASSERT_EQUAL_UINT8(1, *sequencer.getDisplayStepIndex());
    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());
    controller.process(136'000, ticks);
    TEST_ASSERT_EQUAL_UINT8(61, *sequencer.actualSoundingNote());
}

void test_internal_catch_up_is_bounded_and_reported() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    for (std::uint32_t timestamp = 1; timestamp <= 5; ++timestamp) {
        TEST_ASSERT_TRUE(ticks.publish({timestamp, 1'000}));
    }
    controller.process(100, ticks);

    TEST_ASSERT_EQUAL_UINT32(4, controller.diagnostics().lateTicks);
    TEST_ASSERT_EQUAL_UINT32(1, controller.diagnostics().droppedTicks);
    TEST_ASSERT_EQUAL_UINT32(5, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[4][1]);
}

void test_capacity_schedule_failure_stops_once_and_clears_queue() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::TransportController controller{sequencer, settings, sink, queue};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    controller.process(1'000, ticks);
    for (std::size_t index = 0; index < SwingMetro::MidiEventQueue::CAPACITY; ++index) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({100 + index, 0}, {0x09, 0x90, 1, 1}));
    }
    for (std::uint32_t tick = 1; tick <= 4; ++tick) {
        TEST_ASSERT_TRUE(ticks.publish({1'000 + tick * 20'000, 20'000}));
        controller.process(1'000 + tick * 20'000, ticks);
    }

    TEST_ASSERT_FALSE(sequencer.isRunning());
    TEST_ASSERT_TRUE(queue.empty());
    TEST_ASSERT_EQUAL_UINT32(9, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[7][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.packets[8][1]);
}

void test_tick_quota_schedule_failure_stops_once_and_clears_queue() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::TransportController controller{sequencer, settings, sink, queue};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    controller.process(1'000, ticks);
    for (std::size_t index = 0; index < 7; ++index) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({6, 0}, {0x09, 0x90, 1, 1}));
    }
    for (std::uint32_t tick = 1; tick <= 4; ++tick) {
        TEST_ASSERT_TRUE(ticks.publish({1'000 + tick * 20'000, 20'000}));
        controller.process(1'000 + tick * 20'000, ticks);
    }

    TEST_ASSERT_FALSE(sequencer.isRunning());
    TEST_ASSERT_TRUE(queue.empty());
    TEST_ASSERT_EQUAL_UINT32(9, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[7][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.packets[8][1]);
}

void test_swing_orders_clock_off_then_delayed_on() {
    Sequencer sequencer;
    auto steps = sequencer.steps();
    steps[0] = {true, 60, 100};
    steps[1] = {true, 61, 100};
    sequencer.setSteps(steps);
    sequencer.setSwing(75);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    for (std::uint32_t tick = 0; tick <= 6; ++tick) {
        TEST_ASSERT_TRUE(ticks.publish({1'000 + tick * 20'000, 20'000}));
        controller.process(1'000 + tick * 20'000, ticks);
    }
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[sink.count - 2][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[sink.count - 1][1]);
    controller.process(136'000, ticks);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[sink.count - 1][1]);
    TEST_ASSERT_EQUAL_UINT8(61, sink.packets[sink.count - 1][2]);
}

void test_stop_before_delayed_on_does_not_send_an_off() {
    Sequencer sequencer;
    auto steps = sequencer.steps();
    steps[0] = {true, 60, 100};
    steps[1] = {true, 61, 100};
    sequencer.setSteps(steps);
    sequencer.setSwing(90);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    for (std::uint32_t tick = 0; tick <= 6; ++tick) {
        TEST_ASSERT_TRUE(ticks.publish({1'000 + tick * 20'000, 20'000}));
        controller.process(1'000 + tick * 20'000, ticks);
    }
    const auto beforeStop = sink.count;
    controller.toggle(122'000);
    TEST_ASSERT_EQUAL_UINT32(beforeStop + 1, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.packets[sink.count - 1][1]);
    controller.process(200'000, ticks);
    TEST_ASSERT_EQUAL_UINT32(beforeStop + 1, sink.count);
}

void test_stop_after_delayed_on_sends_its_actual_off() {
    Sequencer sequencer;
    auto steps = sequencer.steps();
    steps[0] = {true, 60, 100};
    steps[1] = {true, 61, 100};
    sequencer.setSteps(steps);
    sequencer.setSwing(90);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    for (std::uint32_t tick = 0; tick <= 6; ++tick) {
        TEST_ASSERT_TRUE(ticks.publish({1'000 + tick * 20'000, 20'000}));
        controller.process(1'000 + tick * 20'000, ticks);
    }
    controller.process(139'000, ticks);
    const auto beforeStop = sink.count;
    controller.toggle(140'000);
    TEST_ASSERT_EQUAL_UINT32(beforeStop + 2, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[beforeStop][1]);
    TEST_ASSERT_EQUAL_UINT8(61, sink.packets[beforeStop][2]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.packets[beforeStop + 1][1]);
}

void test_period_changes_retime_deadline_without_changing_queue_position() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink};
    const auto phase = SwingMetro::phaseFromPercent(75);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({1, phase}, {0x09, 0x90, 60, 100}));

    dispatcher.start(false);
    dispatcher.consumeTick({1'000, 20'000}, false);
    dispatcher.consumeTick({21'000, 40'000}, false);
    TEST_ASSERT_EQUAL_UINT64(1, queue.nextPosition()->tick);
    TEST_ASSERT_EQUAL_UINT16(phase, queue.nextPosition()->phase);
    dispatcher.dispatchDue(50'999);
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
    dispatcher.dispatchDue(51'000);
    TEST_ASSERT_EQUAL_UINT32(1, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[0][1]);
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
    RUN_TEST(test_external_loss_relocks_with_one_clock_before_continue);
    RUN_TEST(test_display_step_changes_at_boundary_not_while_scheduling);
    RUN_TEST(test_internal_catch_up_is_bounded_and_reported);
    RUN_TEST(test_capacity_schedule_failure_stops_once_and_clears_queue);
    RUN_TEST(test_tick_quota_schedule_failure_stops_once_and_clears_queue);
    RUN_TEST(test_swing_orders_clock_off_then_delayed_on);
    RUN_TEST(test_stop_before_delayed_on_does_not_send_an_off);
    RUN_TEST(test_stop_after_delayed_on_sends_its_actual_off);
    RUN_TEST(test_period_changes_retime_deadline_without_changing_queue_position);
}
