#include "test_transport_controller.h"

#include "drivers/midi_usb_encoder.h"
#include "engine/transport_controller.h"

#include <array>
#include <unity.h>

namespace {

class Sink final : public SwingMetro::MidiMessageSink {
  public:
    auto send(const SwingMetro::MidiMessage& message) -> SwingMetro::SendResult override {
        packets[count++] = SwingMetro::encodeMidiUsbPacket(message);
        return attemptCount < results.size() ? results[attemptCount++]
                                             : SwingMetro::SendResult::Accepted;
    }

    std::array<SwingMetro::MidiUsbPacket, 128> packets{};
    std::array<SwingMetro::SendResult, 128> results{};
    std::size_t count = 0;
    std::size_t attemptCount = 0;
};

constexpr auto noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity)
    -> SwingMetro::MidiMessage {
    return *SwingMetro::MidiMessage::noteOn(channel, note, velocity);
}

auto event(SwingMetro::MidiRealtimeEventType type, std::uint32_t timestampUs)
    -> SwingMetro::MidiRealtimeEvent {
    return {type, timestampUs};
}

void enableFirstStep(Sequencer& sequencer) { sequencer.toggleStep(0); }

void handleExternal(SwingMetro::TransportController& controller,
                    SwingMetro::MidiRealtimeEventType type, std::uint32_t timestampUs) {
    controller.handleExternal(event(type, timestampUs), timestampUs);
}

void test_dispatcher_sends_tick_before_phase_zero_and_due_phase() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, SwingMetro::phaseFromPercent(50)}, noteOn(0, 60, 100)));

    dispatcher.start(true);
    dispatcher.consumeTick({1'000, 20'000}, true, 1'000);
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[1][1]);
    dispatcher.dispatchDue(11'000, 11'000);
    TEST_ASSERT_EQUAL_UINT32(3, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[2][1]);
}

void test_velocity_zero_note_on_clears_sounding_note_without_changing_bytes() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    sequencer.notifyNoteOnAccepted(60);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, 0}, noteOn(0, 60, 0)));

    dispatcher.start(false);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);

    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());
    TEST_ASSERT_EQUAL_HEX8(0x09, sink.packets[0][0]);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_UINT8(60, sink.packets[0][2]);
    TEST_ASSERT_EQUAL_UINT8(0, sink.packets[0][3]);
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
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 0);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 1'000);
    controller.process(1'000, ticks);
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 21'833);
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
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 0);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 1'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 21'833);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 22'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 42'833);

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
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 30'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 31'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 51'833);
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
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 0);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 1'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 21'833);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Stop, 22'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Continue, 23'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 42'666);
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
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 0);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 1'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 21'833);
    controller.process(21'833 + SwingMetro::ExternalMidiClock::CLOCK_LOSS_TIMEOUT_US, ticks);
    TEST_ASSERT_FALSE(sequencer.isRunning());

    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 300'000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<std::uint8_t>(controller.externalStatus()));
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Continue, 300'001);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 320'833);
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

void test_internal_backlog_is_bounded_per_pass_and_retained() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickSource source;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    (void)source.start(0, 120);
    for (std::uint32_t tick = 0; tick < 5; ++tick) {
        const auto request = source.alarmRequest();
        (void)source.onAlarm(request, request.deadlineUs);
    }
    controller.process(100, source.ticks());

    auto diagnostics = controller.pipelineDiagnostics(source);
    TEST_ASSERT_EQUAL_UINT32(4, diagnostics.successfulConsumerPops);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.budgetDiscards);
    TEST_ASSERT_EQUAL_UINT32(4, diagnostics.outgoingInternalClockAttempts);
    TEST_ASSERT_EQUAL_UINT32(4, controller.diagnostics().maxInternalTicksPoppedPerProcessPass);
    TEST_ASSERT_EQUAL_UINT32(1, controller.diagnostics().internalTickBudgetReachedPasses);
    TEST_ASSERT_EQUAL_UINT32(2, controller.diagnostics().maxRemainingInternalTicksAfterBudgetPass);
    TEST_ASSERT_EQUAL_UINT32(5, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[4][1]);

    controller.process(101, source.ticks());
    diagnostics = controller.pipelineDiagnostics(source);
    TEST_ASSERT_EQUAL_UINT32(6, diagnostics.successfulConsumerPops);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.budgetDiscards);
    TEST_ASSERT_EQUAL_UINT32(6, diagnostics.outgoingInternalClockAttempts);
    TEST_ASSERT_EQUAL_UINT32(7, sink.count);
}

void test_internal_tick_pipeline_balances_start_callbacks_and_clock_attempts() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickSource source;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    (void)source.start(0, 120);
    controller.process(0, source.ticks());
    for (std::uint32_t tick = 1; tick <= 3; ++tick) {
        const auto request = source.alarmRequest();
        (void)source.onAlarm(request, request.deadlineUs);
        controller.process(request.deadlineUs, source.ticks());
    }

    const auto diagnostics = controller.pipelineDiagnostics(source);
    TEST_ASSERT_EQUAL_UINT32(3, diagnostics.producer.alarmCallbackInvocations);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.producer.synchronousStartPublicationAttempts);
    TEST_ASSERT_EQUAL_UINT32(4, diagnostics.producer.successfulPublications);
    TEST_ASSERT_EQUAL_UINT32(4, diagnostics.successfulConsumerPops);
    TEST_ASSERT_EQUAL_UINT32(4, diagnostics.outgoingInternalClockAttempts);
}

void test_internal_source_uses_scheduled_timestamp_for_f8_lateness() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickSource source;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    (void)source.start(0, 120);
    controller.process(0, source.ticks());
    const auto request = source.alarmRequest();
    (void)source.onAlarm(request, request.deadlineUs + 10);
    controller.process(request.deadlineUs + 125, source.ticks());

    TEST_ASSERT_EQUAL_UINT32(125, controller.diagnostics().maxInternalTickProcessingLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(125, controller.diagnostics().maxClockAttemptLatenessUs);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[sink.count - 1][1]);
}

void test_internal_tick_pipeline_does_not_discard_below_processing_budget() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickSource source;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    (void)source.start(0, 120);
    controller.process(0, source.ticks());

    const auto diagnostics = controller.pipelineDiagnostics(source);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.successfulConsumerPops);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.budgetDiscards);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.outgoingInternalClockAttempts);
}

void test_internal_tick_backlog_spans_multiple_passes_without_loss_or_duplicates() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickSource source;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    (void)source.start(0, 120);
    for (std::uint32_t tick = 0; tick < 12; ++tick) {
        const auto request = source.alarmRequest();
        (void)source.onAlarm(request, request.deadlineUs);
    }
    for (std::uint32_t pass = 0; pass < 4; ++pass) {
        controller.process(100 + pass, source.ticks());
    }
    const auto diagnostics = controller.pipelineDiagnostics(source);
    TEST_ASSERT_EQUAL_UINT32(13, diagnostics.successfulConsumerPops);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.budgetDiscards);
    TEST_ASSERT_EQUAL_UINT32(13, diagnostics.outgoingInternalClockAttempts);
    TEST_ASSERT_EQUAL_UINT32(13, source.diagnostics().successfulPublications);
    TEST_ASSERT_EQUAL_UINT32(4, controller.diagnostics().maxInternalTicksPoppedPerProcessPass);
    TEST_ASSERT_EQUAL_UINT32(3, controller.diagnostics().internalTickBudgetReachedPasses);
    TEST_ASSERT_EQUAL_UINT32(9, controller.diagnostics().maxRemainingInternalTicksAfterBudgetPass);
    TEST_ASSERT_EQUAL_UINT32(14, sink.count);
}

void test_internal_tick_published_after_empty_pass_is_retained() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    controller.process(0, ticks);
    TEST_ASSERT_TRUE(ticks.publish({1, 1'000}));
    controller.process(1, ticks);
    TEST_ASSERT_EQUAL_UINT32(1, controller.diagnostics().successfulInternalTickPops);
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[1][1]);
}

void test_internal_tick_state_changes_discard_without_clock() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickSource source;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    (void)source.start(0, 120);

    controller.toggle(101);
    source.stop(controller.internalTickDiscardReason());
    TEST_ASSERT_EQUAL_UINT32(1, controller.pipelineDiagnostics(source).producer.stopDiscards);

    controller.toggle(102);
    (void)source.start(102, 120);
    controller.applyMode(SwingMetro::MidiClockMode::External);
    source.stop(controller.internalTickDiscardReason());
    TEST_ASSERT_EQUAL_UINT32(1, controller.pipelineDiagnostics(source).producer.modeSwitchDiscards);

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(104);
    (void)source.start(104, 120);
    controller.openStorage();
    source.stop(controller.internalTickDiscardReason());
    const auto diagnostics = controller.pipelineDiagnostics(source);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.producer.storageDiscards);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.outgoingInternalClockAttempts);
}

void test_process_duration_is_recorded_separately_from_service_interval() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.process(1'000, ticks);
    controller.recordProcessDuration(1'000, 1'025);
    controller.process(2'000, ticks);
    controller.recordProcessDuration(2'000, 2'010);
    TEST_ASSERT_EQUAL_UINT32(1'000, controller.diagnostics().maxServiceIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(25, controller.diagnostics().maxProcessDurationUs);
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
                          queue.enqueue({100 + index, 0}, noteOn(0, 1, 1)));
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
                          queue.enqueue({6, 0}, noteOn(0, 1, 1)));
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
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    const auto phase = SwingMetro::phaseFromPercent(75);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({1, phase}, noteOn(0, 60, 100)));

    dispatcher.start(false);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    dispatcher.consumeTick({21'000, 40'000}, false, 21'000);
    TEST_ASSERT_EQUAL_UINT64(1, queue.nextPosition()->tick);
    TEST_ASSERT_EQUAL_UINT16(phase, queue.nextPosition()->phase);
    dispatcher.dispatchDue(50'999, 50'999);
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
    dispatcher.dispatchDue(51'000, 51'000);
    TEST_ASSERT_EQUAL_UINT32(1, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[0][1]);
}

void test_dispatcher_does_not_send_phase_before_tick_start_or_deadline() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    const auto phase = SwingMetro::phaseFromPercent(75);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phase}, noteOn(0, 60, 100)));

    dispatcher.start(false);
    dispatcher.consumeTick({100'010, 20'000}, false, 100'010);
    dispatcher.dispatchDue(100'000, 100'000);
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
    dispatcher.dispatchDue(100'010, 100'010);
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
    dispatcher.dispatchDue(115'009, 115'009);
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
    dispatcher.dispatchDue(115'010, 115'010);
    TEST_ASSERT_EQUAL_UINT32(1, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[0][1]);
}

void test_dispatcher_sends_phase_after_timestamp_wrap() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    const auto phase = SwingMetro::phaseFromPercent(75);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phase}, noteOn(0, 60, 100)));

    dispatcher.start(false);
    dispatcher.consumeTick({UINT32_MAX - 9'999, 20'000}, false, UINT32_MAX - 9'999);
    dispatcher.dispatchDue(4'999, 4'999);
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
    dispatcher.dispatchDue(5'000, 5'000);
    TEST_ASSERT_EQUAL_UINT32(1, sink.count);
}

void test_internal_tick_newer_than_process_timestamp_waits_for_deadline() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::TransportController controller{sequencer, settings, sink, queue};
    SwingMetro::InternalTickStore<> ticks;
    const auto phase = SwingMetro::phaseFromPercent(75);

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phase}, noteOn(0, 61, 100)));
    TEST_ASSERT_TRUE(ticks.publish({100'010, 20'000}));
    controller.process(100'000, ticks);
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    controller.process(115'010, ticks);
    TEST_ASSERT_EQUAL_UINT32(3, sink.count);
    TEST_ASSERT_EQUAL_UINT8(61, sink.packets[2][2]);
}

void test_external_tick_newer_than_process_timestamp_waits_for_deadline() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::TransportController controller{sequencer, settings, sink, queue};
    SwingMetro::InternalTickStore<> ticks;
    const auto phase = SwingMetro::phaseFromPercent(75);

    controller.applyMode(SwingMetro::MidiClockMode::External);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 0);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phase}, noteOn(0, 62, 100)));
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 80'010);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 100'010);
    controller.process(100'000, ticks);
    TEST_ASSERT_EQUAL_UINT32(0, sink.count);
    controller.process(115'010, ticks);
    TEST_ASSERT_EQUAL_UINT32(1, sink.count);
    TEST_ASSERT_EQUAL_UINT8(62, sink.packets[0][2]);
}

void test_service_interval_measures_all_process_calls_without_resetting_baseline() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.process(1'000, ticks);
    TEST_ASSERT_EQUAL_UINT32(0, controller.diagnostics().maxServiceIntervalUs);
    controller.toggle(0);
    controller.toggle(0);
    controller.process(1'100, ticks);
    controller.openStorage();
    controller.process(1'500, ticks);
    TEST_ASSERT_EQUAL_UINT32(400, controller.diagnostics().maxServiceIntervalUs);
}

void test_tick_processing_lateness_uses_local_observations() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    controller.process(1'125, ticks);
    TEST_ASSERT_EQUAL_UINT32(125, controller.diagnostics().maxInternalTickProcessingLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(125, controller.diagnostics().maxClockAttemptLatenessUs);

    controller.applyMode(SwingMetro::MidiClockMode::External);
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Start, 30'000), 30'000);
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 31'000), 31'000);
    controller.handleExternal(event(SwingMetro::MidiRealtimeEventType::Clock, 51'000), 51'125);
    TEST_ASSERT_EQUAL_UINT32(125, controller.diagnostics().maxExternalTickProcessingLatenessUs);
}

void test_queued_event_attempt_lateness_uses_ceil_phase_deadline() {
    const auto phase = SwingMetro::phaseFromPercent(75);
    TEST_ASSERT_EQUAL_UINT32(15'000, SwingMetro::phaseOffsetUs(phase, 20'000));
    TEST_ASSERT_EQUAL_UINT32(1, SwingMetro::phaseOffsetUs(1, 20'000));

    for (const auto attemptAtUs : {15'999U, 16'000U, 16'025U}) {
        Sequencer sequencer;
        SwingMetro::MidiEventQueue queue;
        SwingMetro::Transport transport;
        Sink sink;
        SwingMetro::TransportDiagnostics diagnostics;
        SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({0, phase}, noteOn(0, 60, 100)));

        dispatcher.start(false);
        dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
        dispatcher.dispatchDue(attemptAtUs, attemptAtUs);
        TEST_ASSERT_EQUAL_UINT32(attemptAtUs == 16'025U ? 25 : 0,
                                 diagnostics.maxQueuedEventAttemptLatenessUs);
    }
}

void test_attempt_lateness_classifies_queued_clock_and_wraps() {
    const auto phase = SwingMetro::phaseFromPercent(75);
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phase}, SwingMetro::MidiMessage::clock()));
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phase}, noteOn(0, 60, 100)));

    dispatcher.start(false);
    dispatcher.consumeTick({UINT32_MAX - 9'999, 20'000}, false, UINT32_MAX - 9'999);
    dispatcher.dispatchDue(5'025, 5'025);
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[1][1]);
    TEST_ASSERT_EQUAL_UINT32(25, diagnostics.maxClockAttemptLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(25, diagnostics.maxQueuedEventAttemptLatenessUs);
}

void test_service_and_tick_lateness_wrap_and_rebaseline_at_horizon() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.process(UINT32_MAX - 100, ticks);
    controller.process(50, ticks);
    TEST_ASSERT_EQUAL_UINT32(151, controller.diagnostics().maxServiceIntervalUs);

    const auto antipodal = 50U + SwingMetro::TIMESTAMP_COMPARISON_HORIZON_US;
    controller.process(antipodal, ticks);
    controller.process(antipodal + 25U, ticks);
    TEST_ASSERT_EQUAL_UINT32(151, controller.diagnostics().maxServiceIntervalUs);

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_TRUE(ticks.publish({UINT32_MAX - 50, 20'000}));
    controller.process(75, ticks);
    TEST_ASSERT_EQUAL_UINT32(126, controller.diagnostics().maxInternalTickProcessingLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(126, controller.diagnostics().maxClockAttemptLatenessUs);
}

void test_retry_keeps_delivery_identity_and_commits_note_once() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results[0] = SwingMetro::SendResult::RetryLater;
    sink.results[1] = SwingMetro::SendResult::Accepted;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, 0}, noteOn(0, 60, 100)));

    dispatcher.start(false, 900);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    const auto pending = dispatcher.pendingFront();
    TEST_ASSERT_TRUE(pending.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, pending->deliverySequenceNumber);
    TEST_ASSERT_EQUAL_UINT32(1'000, pending->deadlineUs);
    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());

    dispatcher.dispatchDue(1'001, 1'001);
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_FALSE(dispatcher.pendingFront().has_value());
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());
}

void test_one_pass_limits_accepted_fifo_to_eight_attempts() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    for (std::size_t index = 0; index < 7; ++index) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({0, 0}, noteOn(0, 60, 100)));
    }
    for (std::size_t index = 0; index < 2; ++index) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({1, 0}, noteOn(0, 61, 100)));
    }

    dispatcher.start(false, 0);
    dispatcher.beginPass(1'000);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    dispatcher.consumeTick({21'000, 20'000}, false, 21'000);
    dispatcher.finishPass(21'000);
    TEST_ASSERT_EQUAL_UINT32(8, sink.count);
    TEST_ASSERT_TRUE(dispatcher.pendingFront().has_value());
}

void test_full_pending_keeps_scheduled_head_and_reports_capacity() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results[0] = SwingMetro::SendResult::RetryLater;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    for (std::size_t tick = 0; tick < SwingMetro::MidiEventQueue::CAPACITY; ++tick) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({tick, 0}, noteOn(0, 60, 100)));
    }

    dispatcher.start(false, 0);
    dispatcher.beginPass(0);
    for (std::uint32_t tick = 0; tick < SwingMetro::MidiEventQueue::CAPACITY; ++tick) {
        dispatcher.consumeTick({1'000 + tick * 20'000, 20'000}, false, 1'000 + tick * 20'000);
    }
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({16, 0}, noteOn(0, 61, 100)));
    dispatcher.consumeTick({321'000, 20'000}, false, 321'000);
    TEST_ASSERT_TRUE(dispatcher.capacityExceeded());
    TEST_ASSERT_EQUAL_UINT64(16, queue.nextPosition()->tick);
}

void test_full_pending_does_not_discard_previous_tick_scheduled_head() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    for (std::size_t tick = 0; tick < SwingMetro::MidiEventQueue::CAPACITY - 1; ++tick) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({tick, 0}, noteOn(0, 60, 100)));
    }
    const auto delayedPhase = SwingMetro::phaseFromPercent(75);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({15, delayedPhase}, noteOn(0, 61, 100)));

    dispatcher.beginPass(0);
    dispatcher.start(true, 0);
    for (std::uint32_t tick = 0; tick <= 15; ++tick) {
        dispatcher.consumeTick({1'000 + tick * 20'000, 20'000}, false, 1'000 + tick * 20'000);
    }
    dispatcher.consumeTick({321'000, 20'000}, false, 321'000);

    TEST_ASSERT_TRUE(dispatcher.capacityExceeded());
    TEST_ASSERT_EQUAL_UINT64(15, queue.front()->target.tick);
    TEST_ASSERT_EQUAL_UINT16(delayedPhase, queue.front()->target.phase);
}

void test_retry_and_disconnect_attempt_head_only_once_per_pass() {
    for (const auto blockedResult :
         {SwingMetro::SendResult::RetryLater, SwingMetro::SendResult::Disconnected}) {
        Sequencer sequencer;
        SwingMetro::MidiEventQueue queue;
        SwingMetro::Transport transport;
        Sink sink;
        sink.results[0] = blockedResult;
        sink.results[1] = SwingMetro::SendResult::Accepted;
        SwingMetro::TransportDiagnostics diagnostics;
        SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({0, 0}, noteOn(0, 60, 100)));

        dispatcher.start(false, 900);
        dispatcher.beginPass(1'000);
        dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
        dispatcher.finishPass(1'000);
        TEST_ASSERT_EQUAL_UINT32(1, sink.count);
        const auto blocked = dispatcher.pendingFront();
        TEST_ASSERT_TRUE(blocked.has_value());
        TEST_ASSERT_EQUAL_UINT32(0, blocked->deliverySequenceNumber);
        TEST_ASSERT_EQUAL_UINT32(1'000, blocked->deadlineUs);
        TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());

        dispatcher.beginPass(1'001);
        dispatcher.finishPass(1'001);
        TEST_ASSERT_EQUAL_UINT32(2, sink.count);
        TEST_ASSERT_FALSE(dispatcher.pendingFront().has_value());
        TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());
    }
}

void test_blocked_start_prevents_clock_from_overtaking() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results[0] = SwingMetro::SendResult::RetryLater;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};

    dispatcher.beginPass(100);
    dispatcher.start(true, 100);
    dispatcher.consumeTick({100, 20'000}, true, 100);
    dispatcher.finishPass(100);
    TEST_ASSERT_EQUAL_UINT32(1, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.outgoingInternalClockAttempts);

    dispatcher.beginPass(101);
    dispatcher.finishPass(101);
    TEST_ASSERT_EQUAL_UINT32(3, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.packets[1][1]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.packets[2][1]);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.outgoingInternalClockAttempts);
}

void test_blocked_note_off_prevents_note_on_and_commits_in_order() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, 0}, noteOn(0, 60, 100)));
    dispatcher.start(false, 0);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());

    sink.results[1] = SwingMetro::SendResult::RetryLater;
    sink.results[2] = SwingMetro::SendResult::Accepted;
    sink.results[3] = SwingMetro::SendResult::RetryLater;
    sink.results[4] = SwingMetro::SendResult::Accepted;
    const std::array<SwingMetro::MidiEventRequest, 2> next = {
        SwingMetro::MidiEventRequest{{1, 0}, *SwingMetro::MidiMessage::noteOff(0, 60)},
        SwingMetro::MidiEventRequest{{1, 0}, noteOn(0, 61, 100)},
    };
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueueBatch(next, next.size()));

    dispatcher.beginPass(21'000);
    dispatcher.consumeTick({21'000, 20'000}, false, 21'000);
    dispatcher.finishPass(21'000);
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[1][1]);
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());

    dispatcher.beginPass(21'001);
    dispatcher.finishPass(21'001);
    TEST_ASSERT_EQUAL_UINT32(4, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[2][1]);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[3][1]);
    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());

    dispatcher.beginPass(21'002);
    dispatcher.finishPass(21'002);
    TEST_ASSERT_EQUAL_UINT32(5, sink.count);
    TEST_ASSERT_EQUAL_UINT8(61, *sequencer.actualSoundingNote());
}

void test_velocity_zero_note_on_clears_state_only_after_acceptance() {
    Sequencer sequencer;
    sequencer.notifyNoteOnAccepted(60);
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results[0] = SwingMetro::SendResult::RetryLater;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, 0}, noteOn(0, 60, 0)));

    dispatcher.start(false, 0);
    dispatcher.beginPass(1'000);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    dispatcher.finishPass(1'000);
    TEST_ASSERT_EQUAL_UINT8(60, *sequencer.actualSoundingNote());

    dispatcher.beginPass(1'001);
    dispatcher.finishPass(1'001);
    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());
}

void test_capacity_stop_cancels_without_recursive_send_and_can_restart() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    SwingMetro::MidiEventQueue queue;
    Sink sink;
    sink.results.fill(SwingMetro::SendResult::RetryLater);
    SwingMetro::TransportController controller{sequencer, settings, sink, queue};
    SwingMetro::InternalTickStore<> ticks;
    controller.applyMode(SwingMetro::MidiClockMode::Internal, 0);
    controller.toggle(1);
    for (std::size_t tick = 0; tick < SwingMetro::MidiEventQueue::CAPACITY; ++tick) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({tick, 0}, noteOn(0, 60, 100)));
    }

    for (std::uint32_t tick = 0;
         tick < SwingMetro::MidiEventQueue::CAPACITY && controller.usesInternalTiming(); ++tick) {
        const auto timestampUs = 1'000 + tick * 20'000;
        TEST_ASSERT_TRUE(ticks.publish({timestampUs, 20'000}));
        const auto attemptsBefore = sink.count;
        controller.process(timestampUs, ticks);
        TEST_ASSERT_TRUE(sink.count == attemptsBefore || sink.count == attemptsBefore + 1);
    }

    TEST_ASSERT_FALSE(controller.usesInternalTiming());
    TEST_ASSERT_TRUE(queue.empty());
    for (std::size_t index = 0; index < sink.count; ++index) {
        TEST_ASSERT_EQUAL_HEX8(0xFA, sink.packets[index][1]);
    }
    const auto attemptsAfterCapacityStop = sink.count;
    controller.toggle(400'000);
    TEST_ASSERT_TRUE(controller.usesInternalTiming());
    TEST_ASSERT_EQUAL_UINT32(attemptsAfterCapacityStop + 1, sink.count);
}

void test_direct_control_messages_do_not_change_stage_one_lateness_metrics() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};

    dispatcher.beginPass(10'000);
    dispatcher.start(true, 100);
    dispatcher.finishPass(10'000);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.maxClockAttemptLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.maxQueuedEventAttemptLatenessUs);
}

} // namespace

void test_transport_controller_main() {
    RUN_TEST(test_dispatcher_sends_tick_before_phase_zero_and_due_phase);
    RUN_TEST(test_velocity_zero_note_on_clears_sounding_note_without_changing_bytes);
    RUN_TEST(test_internal_start_emits_start_tick_and_queued_note);
    RUN_TEST(test_external_start_waits_for_measured_tick_and_never_echoes_clock);
    RUN_TEST(test_repeated_external_start_stops_sounding_note_before_reset);
    RUN_TEST(test_stop_switch_and_loss_send_one_note_off_without_realtime_leak);
    RUN_TEST(test_local_toggle_starts_then_stops_external_without_continue_packet);
    RUN_TEST(test_external_continue_waits_for_next_tick_without_retriggering);
    RUN_TEST(test_external_loss_relocks_with_one_clock_before_continue);
    RUN_TEST(test_display_step_changes_at_boundary_not_while_scheduling);
    RUN_TEST(test_internal_backlog_is_bounded_per_pass_and_retained);
    RUN_TEST(test_internal_tick_pipeline_balances_start_callbacks_and_clock_attempts);
    RUN_TEST(test_internal_source_uses_scheduled_timestamp_for_f8_lateness);
    RUN_TEST(test_internal_tick_pipeline_does_not_discard_below_processing_budget);
    RUN_TEST(test_internal_tick_backlog_spans_multiple_passes_without_loss_or_duplicates);
    RUN_TEST(test_internal_tick_published_after_empty_pass_is_retained);
    RUN_TEST(test_internal_tick_state_changes_discard_without_clock);
    RUN_TEST(test_capacity_schedule_failure_stops_once_and_clears_queue);
    RUN_TEST(test_tick_quota_schedule_failure_stops_once_and_clears_queue);
    RUN_TEST(test_swing_orders_clock_off_then_delayed_on);
    RUN_TEST(test_stop_before_delayed_on_does_not_send_an_off);
    RUN_TEST(test_stop_after_delayed_on_sends_its_actual_off);
    RUN_TEST(test_period_changes_retime_deadline_without_changing_queue_position);
    RUN_TEST(test_dispatcher_does_not_send_phase_before_tick_start_or_deadline);
    RUN_TEST(test_dispatcher_sends_phase_after_timestamp_wrap);
    RUN_TEST(test_internal_tick_newer_than_process_timestamp_waits_for_deadline);
    RUN_TEST(test_external_tick_newer_than_process_timestamp_waits_for_deadline);
    RUN_TEST(test_service_interval_measures_all_process_calls_without_resetting_baseline);
    RUN_TEST(test_tick_processing_lateness_uses_local_observations);
    RUN_TEST(test_queued_event_attempt_lateness_uses_ceil_phase_deadline);
    RUN_TEST(test_attempt_lateness_classifies_queued_clock_and_wraps);
    RUN_TEST(test_service_and_tick_lateness_wrap_and_rebaseline_at_horizon);
    RUN_TEST(test_retry_keeps_delivery_identity_and_commits_note_once);
    RUN_TEST(test_one_pass_limits_accepted_fifo_to_eight_attempts);
    RUN_TEST(test_full_pending_keeps_scheduled_head_and_reports_capacity);
    RUN_TEST(test_full_pending_does_not_discard_previous_tick_scheduled_head);
    RUN_TEST(test_retry_and_disconnect_attempt_head_only_once_per_pass);
    RUN_TEST(test_blocked_start_prevents_clock_from_overtaking);
    RUN_TEST(test_blocked_note_off_prevents_note_on_and_commits_in_order);
    RUN_TEST(test_velocity_zero_note_on_clears_state_only_after_acceptance);
    RUN_TEST(test_capacity_stop_cancels_without_recursive_send_and_can_restart);
    RUN_TEST(test_direct_control_messages_do_not_change_stage_one_lateness_metrics);
    RUN_TEST(test_process_duration_is_recorded_separately_from_service_interval);
}
