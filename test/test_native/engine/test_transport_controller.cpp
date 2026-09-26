#include "test_transport_controller.h"

#include "drivers/midi_usb_encoder.h"
#include "engine/fault_midi_message_sink.h"
#include "engine/transport_controller.h"

#include <array>
#include <unity.h>

namespace {

class Sink final : public SwingMetro::MidiMessageSink {
  public:
    auto send(const SwingMetro::MidiDeliveryAttempt& attempt) -> SwingMetro::SendResult override {
        packets[count] = SwingMetro::encodeMidiUsbPacket(attempt.message);
        attempts[count] = attempt;
        ++count;
        return attemptCount < results.size() ? results[attemptCount++]
                                             : SwingMetro::SendResult::Accepted;
    }

    std::array<SwingMetro::MidiUsbPacket, 128> packets{};
    std::array<SwingMetro::MidiDeliveryAttempt, 128> attempts{};
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

auto classIndex(SwingMetro::MidiMessageClass messageClass) -> std::size_t {
    return static_cast<std::size_t>(messageClass);
}

auto pendingRemovalCount(const SwingMetro::TransportDiagnostics& diagnostics,
                         SwingMetro::MidiMessageClass messageClass) -> std::uint32_t {
    std::uint32_t count = 0;
    for (const auto& reason : diagnostics.pendingRemovals) {
        count += reason[classIndex(messageClass)];
    }
    return count;
}

auto scheduledRemovalCount(const SwingMetro::TransportDiagnostics& diagnostics,
                           SwingMetro::MidiMessageClass messageClass) -> std::uint32_t {
    std::uint32_t count = 0;
    for (const auto& reason : diagnostics.scheduledRemovals) {
        count += reason[classIndex(messageClass)];
    }
    return count;
}

void assertOutboxBalance(const SwingMetro::TransportDiagnostics& diagnostics,
                         SwingMetro::MidiMessageClass messageClass) {
    const auto index = classIndex(messageClass);
    TEST_ASSERT_EQUAL_UINT32(diagnostics.outboxInserted[index],
                             diagnostics.currentOutboxDepthByClass[index] +
                                 diagnostics.delivery[index].accepted +
                                 pendingRemovalCount(diagnostics, messageClass));
}

void assertScheduledBalance(const SwingMetro::TransportDiagnostics& diagnostics,
                            SwingMetro::MidiMessageClass messageClass) {
    const auto index = classIndex(messageClass);
    TEST_ASSERT_EQUAL_UINT32(diagnostics.scheduledCreated[index],
                             diagnostics.currentScheduledDepth[index] +
                                 diagnostics.scheduledTransferred[index] +
                                 scheduledRemovalCount(diagnostics, messageClass));
}

void assertDeliveryBalances(const SwingMetro::TransportDiagnostics& diagnostics,
                            SwingMetro::MidiMessageClass messageClass) {
    assertOutboxBalance(diagnostics, messageClass);
    assertScheduledBalance(diagnostics, messageClass);
}

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
    sequencer.toggleStep(1);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::TransportController controller{sequencer, settings, sink, queue};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    controller.process(1'000, ticks);
    (void)queue.clear();
    for (std::size_t index = 0; index < SwingMetro::MidiEventQueue::CAPACITY; ++index) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({100 + index, 0}, noteOn(0, 1, 1)));
    }
    for (std::uint32_t tick = 1; tick <= 5; ++tick) {
        TEST_ASSERT_TRUE(ticks.publish({1'000 + tick * 20'000, 20'000}));
        controller.process(1'000 + tick * 20'000, ticks);
    }

    TEST_ASSERT_FALSE(sequencer.isRunning());
    TEST_ASSERT_TRUE(queue.empty());
    TEST_ASSERT_EQUAL_UINT32(9, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[7][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.packets[8][1]);
    TEST_ASSERT_EQUAL_UINT32(1, controller.diagnostics().deliveryCapacitySafetyStops);
}

void test_tick_quota_schedule_failure_stops_once_and_clears_queue() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    sequencer.toggleStep(1);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::TransportController controller{sequencer, settings, sink, queue};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal);
    controller.toggle(0);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    controller.process(1'000, ticks);
    (void)queue.clear();
    for (std::size_t index = 0; index < 7; ++index) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({6, 0}, noteOn(0, 1, 1)));
    }
    for (std::uint32_t tick = 1; tick <= 5; ++tick) {
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
    const auto clock = classIndex(SwingMetro::MidiMessageClass::Clock);
    const auto note = classIndex(SwingMetro::MidiMessageClass::Note);
    TEST_ASSERT_EQUAL_UINT32(25, diagnostics.delivery[clock].maxFirstAttemptLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(25, diagnostics.delivery[clock].maxAcceptanceLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(25, diagnostics.delivery[note].maxFirstAttemptLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(25, diagnostics.delivery[note].maxAcceptanceLatenessUs);
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
    TEST_ASSERT_EQUAL_UINT32(8, diagnostics.maxSendAttemptsPerPublicPass);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.currentOutboxDepth);
    TEST_ASSERT_EQUAL_UINT32(7, diagnostics.maxOutboxDepth);
}

void test_full_pending_keeps_scheduled_head_and_reports_capacity() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results[0] = SwingMetro::SendResult::RetryLater;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    for (std::size_t tick = 0; tick < SwingMetro::MidiPendingDeliveryQueue::NORMAL_CAPACITY;
         ++tick) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({tick, 0}, noteOn(0, 60, 100)));
    }

    dispatcher.start(false, 0);
    dispatcher.beginPass(0);
    for (std::uint32_t tick = 0; tick < SwingMetro::MidiPendingDeliveryQueue::NORMAL_CAPACITY;
         ++tick) {
        dispatcher.consumeTick({1'000 + tick * 20'000, 20'000}, false, 1'000 + tick * 20'000);
    }
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({14, 0}, noteOn(0, 61, 100)));
    dispatcher.consumeTick({281'000, 20'000}, false, 281'000);
    TEST_ASSERT_TRUE(dispatcher.capacityExceeded());
    TEST_ASSERT_EQUAL_UINT64(14, queue.nextPosition()->tick);
}

void test_full_pending_does_not_discard_previous_tick_scheduled_head() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    for (std::size_t tick = 0; tick < SwingMetro::MidiPendingDeliveryQueue::NORMAL_CAPACITY;
         ++tick) {
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({tick, 0}, noteOn(0, 60, 100)));
    }
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({14, SwingMetro::phaseFromPercent(75)}, noteOn(0, 61, 100)));

    dispatcher.beginPass(0);
    dispatcher.start(true, 0);
    for (std::uint32_t tick = 0; tick <= 14; ++tick) {
        dispatcher.consumeTick({1'000 + tick * 20'000, 20'000}, false, 1'000 + tick * 20'000);
    }
    dispatcher.consumeTick({281'000, 20'000}, false, 281'000);

    TEST_ASSERT_FALSE(dispatcher.capacityExceeded());
    TEST_ASSERT_FALSE(queue.front().has_value());
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

        if (blockedResult == SwingMetro::SendResult::Disconnected) {
            TEST_ASSERT_EQUAL(
                static_cast<std::uint8_t>(SwingMetro::DeliveryLifecycleOutcome::Disconnected),
                static_cast<std::uint8_t>(dispatcher.takeLifecycleOutcome()));
            dispatcher.beginPass(1'001);
            dispatcher.finishPass(1'001);
            TEST_ASSERT_EQUAL_UINT32(1, sink.count);
            continue;
        }
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
                          queue.enqueue({tick, 0}, *SwingMetro::MidiMessage::noteOff(0, 60)));
    }

    for (std::uint32_t tick = 0;
         tick < SwingMetro::MidiEventQueue::CAPACITY * 2 && controller.usesInternalTiming();
         ++tick) {
        const auto timestampUs = 1'000 + tick * 20'000;
        TEST_ASSERT_TRUE(ticks.publish({timestampUs, 20'000}));
        const auto attemptsBefore = sink.count;
        controller.process(timestampUs, ticks);
        TEST_ASSERT_TRUE(sink.count == attemptsBefore || sink.count == attemptsBefore + 1);
    }

    TEST_ASSERT_FALSE(controller.usesInternalTiming());
    TEST_ASSERT_TRUE(queue.empty());
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(SwingMetro::InvalidationReason::DeliveryCapacity),
                      static_cast<std::uint8_t>(controller.diagnostics().lastSessionEndReason));
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

void test_lifecycle_expiry_and_generation_helpers_are_exact_and_saturating() {
    TEST_ASSERT_EQUAL_UINT64(16, SwingMetro::MidiDispatcher::expiryTick(10));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, SwingMetro::MidiDispatcher::expiryTick(UINT64_MAX - 2));
    TEST_ASSERT_FALSE(SwingMetro::MidiDispatcher::expiredAt(UINT64_MAX - 1, UINT64_MAX - 2));
    TEST_ASSERT_TRUE(SwingMetro::MidiDispatcher::expiredAt(UINT64_MAX, UINT64_MAX - 2));
    TEST_ASSERT_FALSE(SwingMetro::MidiDispatcher::expiredAt(15, 10));
    TEST_ASSERT_TRUE(SwingMetro::MidiDispatcher::expiredAt(16, 10));
    TEST_ASSERT_EQUAL_UINT32(1, SwingMetro::MidiDispatcher::nextSessionGeneration(UINT32_MAX));
    TEST_ASSERT_EQUAL_UINT32(2, SwingMetro::MidiDispatcher::nextSessionGeneration(1));
}

void test_lateness_distributions_cover_edges_wrap_and_message_classes() {
    constexpr std::array<std::uint32_t, 19> lateness = {
        1,   10,    11,    50,    51,    100,    101,    250,     251,    500,
        501, 1'000, 1'001, 5'000, 5'001, 20'000, 20'001, 100'000, 100'001};
    for (std::size_t index = 0; index < lateness.size(); ++index) {
        Sequencer sequencer;
        SwingMetro::MidiEventQueue queue;
        SwingMetro::Transport transport;
        Sink sink;
        SwingMetro::TransportDiagnostics diagnostics;
        SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
        TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                          queue.enqueue({0, 0}, noteOn(0, 60, 100)));
        dispatcher.start(false);
        dispatcher.consumeTick({1'000, 20'000}, false, 1'000 + lateness[index]);
        const auto bucket = index / 2;
        TEST_ASSERT_EQUAL_UINT32(1, diagnostics.noteAttemptLateness.positive[bucket]);
        TEST_ASSERT_EQUAL_UINT32(1, diagnostics.noteAcceptedLateness.positive[bucket]);
        TEST_ASSERT_EQUAL_UINT32(0, diagnostics.clockAttemptLateness.onTime);
    }

    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    dispatcher.start(false);
    dispatcher.consumeTick({UINT32_MAX - 9, 20'000}, true, UINT32_MAX - 9);
    dispatcher.consumeTick({UINT32_MAX - 9, 20'000}, true, UINT32_MAX - 19);
    dispatcher.consumeTick({UINT32_MAX - 9, 20'000}, true,
                           UINT32_MAX - 9 + SwingMetro::TIMESTAMP_COMPARISON_HORIZON_US);
    dispatcher.consumeTick({UINT32_MAX - 9, 20'000}, true, 5);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.clockAttemptLateness.onTime);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.clockAcceptedLateness.onTime);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.clockAttemptLateness.early);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.clockAttemptLateness.unordered);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.clockAttemptLateness.positive[1]);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.clockAcceptedLateness.positive[1]);
}

void test_lateness_distributions_count_retry_once_per_attempt_and_acceptance() {
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
    dispatcher.start(false);
    dispatcher.beginPass(1'010);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'010);
    dispatcher.finishPass(1'010);
    dispatcher.beginPass(1'025);
    dispatcher.finishPass(1'025);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.noteAttemptLateness.positive[0]);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.noteAttemptLateness.positive[1]);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.noteAcceptedLateness.positive[1]);
    TEST_ASSERT_EQUAL_UINT32(1'000, sink.attempts[0].deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(1'000, sink.attempts[1].deadlineUs);

    diagnostics.noteAttemptLateness.onTime = UINT32_MAX;
    diagnostics.noteAcceptedLateness.onTime = UINT32_MAX;
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({1, 0}, noteOn(0, 61, 100)));
    dispatcher.beginPass(21'000);
    dispatcher.consumeTick({21'000, 20'000}, false, 21'000);
    dispatcher.finishPass(21'000);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, diagnostics.noteAttemptLateness.onTime);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, diagnostics.noteAcceptedLateness.onTime);
}

void test_scheduled_depth_tracks_current_and_high_water() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, 0}, noteOn(0, 60, 100)));
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({1, 0}, noteOn(0, 61, 100)));
    dispatcher.recordScheduledDepth();
    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.currentScheduledDepthTotal);
    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.maxScheduledDepth);
    queue.popFront();
    dispatcher.recordScheduledDepth();
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.currentScheduledDepthTotal);
    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.maxScheduledDepth);
}

void test_clock_backpressure_coalesces_to_latest_without_burst() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results.fill(SwingMetro::SendResult::RetryLater);
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    dispatcher.start(false, 0);

    dispatcher.beginPass(1'000);
    dispatcher.consumeTick({1'000, 20'000}, true, 1'000);
    dispatcher.finishPass(1'000);
    dispatcher.beginPass(21'000);
    dispatcher.consumeTick({21'000, 20'000}, true, 21'000);
    dispatcher.finishPass(21'000);

    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.clockCoalescedCount);
    TEST_ASSERT_EQUAL_UINT32(
        1, diagnostics.pendingRemovals[static_cast<std::size_t>(
               SwingMetro::DeliveryRemovalReason::ClockCoalesced)]
                                      [classIndex(SwingMetro::MidiMessageClass::Clock)]);
    TEST_ASSERT_EQUAL_UINT64(1, dispatcher.pendingFront()->event.target.tick);
    sink.results[sink.attemptCount] = SwingMetro::SendResult::Accepted;
    dispatcher.beginPass(21'001);
    dispatcher.finishPass(21'001);
    TEST_ASSERT_FALSE(dispatcher.pendingFront().has_value());
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.currentOutboxDepth);
    assertDeliveryBalances(diagnostics, SwingMetro::MidiMessageClass::Clock);
}

void test_stale_clock_expires_instead_of_replaying() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results.fill(SwingMetro::SendResult::RetryLater);
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    dispatcher.start(false, 0);

    dispatcher.beginPass(1'000);
    dispatcher.consumeTick({1'000, 20'000}, true, 1'000);
    dispatcher.finishPass(1'000);
    dispatcher.beginPass(21'000);
    dispatcher.consumeTick({21'000, 20'000}, false, 21'000);
    dispatcher.finishPass(21'000);
    dispatcher.beginPass(21'001);
    dispatcher.finishPass(21'001);

    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.clockExpiredCount);
    TEST_ASSERT_EQUAL_UINT32(
        1, diagnostics.pendingRemovals[static_cast<std::size_t>(
               SwingMetro::DeliveryRemovalReason::ClockExpired)]
                                      [classIndex(SwingMetro::MidiMessageClass::Clock)]);
    TEST_ASSERT_FALSE(dispatcher.pendingFront().has_value());
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.currentOutboxDepth);
    assertDeliveryBalances(diagnostics, SwingMetro::MidiMessageClass::Clock);
}

void test_note_on_expires_at_retry_window_equality_without_acceptance() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results.fill(SwingMetro::SendResult::RetryLater);
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, 0}, noteOn(0, 60, 100)));
    dispatcher.start(false, 0);
    dispatcher.beginPass(1'000);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    dispatcher.finishPass(1'000);
    for (std::uint32_t tick = 1; tick <= SwingMetro::TICKS_PER_SIXTEENTH; ++tick) {
        const auto atUs = 1'000 + tick * 20'000;
        dispatcher.beginPass(atUs);
        dispatcher.consumeTick({atUs, 20'000}, false, atUs);
        dispatcher.finishPass(atUs);
    }
    dispatcher.beginPass(121'001);
    dispatcher.finishPass(121'001);

    TEST_ASSERT_FALSE(dispatcher.pendingFront().has_value());
    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.noteOnExpiredCount);
    TEST_ASSERT_EQUAL_UINT32(
        1, diagnostics.pendingRemovals[static_cast<std::size_t>(
               SwingMetro::DeliveryRemovalReason::NoteOnExpired)]
                                      [classIndex(SwingMetro::MidiMessageClass::Note)]);
    TEST_ASSERT_EQUAL_UINT32(
        0, diagnostics.pendingRemovals[static_cast<std::size_t>(
               SwingMetro::DeliveryRemovalReason::ClockExpired)]
                                      [classIndex(SwingMetro::MidiMessageClass::Note)]);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.currentOutboxDepth);
    assertOutboxBalance(diagnostics, SwingMetro::MidiMessageClass::Note);
}

void test_overlapping_same_pitch_replaces_with_identity_safe_off() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    dispatcher.start(false, 0);
    sequencer.beginCleanRemoteSession(dispatcher.sessionGeneration());
    const std::array<SwingMetro::MidiEventRequest, 4> events = {
        SwingMetro::MidiEventRequest{
            {0, 0}, noteOn(0, 60, 100), 1, dispatcher.sessionGeneration(), {12, 0}},
        SwingMetro::MidiEventRequest{{12, 0},
                                     *SwingMetro::MidiMessage::noteOff(0, 60),
                                     1,
                                     dispatcher.sessionGeneration(),
                                     {12, 0}},
        SwingMetro::MidiEventRequest{
            {6, 0}, noteOn(0, 60, 100), 2, dispatcher.sessionGeneration(), {12, 0}},
        SwingMetro::MidiEventRequest{{12, 0},
                                     *SwingMetro::MidiMessage::noteOff(0, 60),
                                     2,
                                     dispatcher.sessionGeneration(),
                                     {12, 0}},
    };
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueueBatch(events.data(), events.size()));
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    for (std::uint32_t tick = 1; tick <= 6; ++tick) {
        dispatcher.consumeTick({1'000 + tick * 20'000, 20'000}, false, 1'000 + tick * 20'000);
    }
    TEST_ASSERT_EQUAL_UINT32(3, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[1][1]);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[2][1]);
    TEST_ASSERT_EQUAL_UINT32(2, sequencer.actualSoundingLaunch()->launchId);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.staleGateOffCount);
    for (std::uint32_t tick = 7; tick <= 12; ++tick) {
        dispatcher.consumeTick({1'000 + tick * 20'000, 20'000}, false, 1'000 + tick * 20'000);
    }
    TEST_ASSERT_EQUAL_UINT32(4, sink.count);
    TEST_ASSERT_FALSE(sequencer.actualSoundingNote().has_value());
}

void test_retrying_swung_projection_gets_early_off_before_same_pitch_replacement() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results.fill(SwingMetro::SendResult::RetryLater);
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    dispatcher.start(false, 0);
    sequencer.beginCleanRemoteSession(dispatcher.sessionGeneration());
    const auto swung = SwingMetro::phaseFromPercent(75);
    const std::array<SwingMetro::MidiEventRequest, 4> events = {
        SwingMetro::MidiEventRequest{
            {0, swung}, noteOn(0, 60, 100), 1, dispatcher.sessionGeneration(), {12, swung}},
        SwingMetro::MidiEventRequest{{12, swung},
                                     *SwingMetro::MidiMessage::noteOff(0, 60),
                                     1,
                                     dispatcher.sessionGeneration(),
                                     {12, swung}},
        SwingMetro::MidiEventRequest{
            {6, 0}, noteOn(0, 60, 100), 2, dispatcher.sessionGeneration(), {12, 0}},
        SwingMetro::MidiEventRequest{{12, 0},
                                     *SwingMetro::MidiMessage::noteOff(0, 60),
                                     2,
                                     dispatcher.sessionGeneration(),
                                     {12, 0}},
    };
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueueBatch(events.data(), events.size()));
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    dispatcher.dispatchDue(16'000, 16'000);
    for (std::uint32_t tick = 1; tick <= 6; ++tick) {
        dispatcher.consumeTick({1'000 + tick * 20'000, 20'000}, false, 1'000 + tick * 20'000);
    }

    const auto recoveryStart = sink.count;
    sink.results[sink.attemptCount] = SwingMetro::SendResult::Accepted;
    sink.results[sink.attemptCount + 1] = SwingMetro::SendResult::Accepted;
    sink.results[sink.attemptCount + 2] = SwingMetro::SendResult::Accepted;
    dispatcher.beginPass(121'001);
    dispatcher.finishPass(121'001);

    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[recoveryStart][1]);
    TEST_ASSERT_EQUAL_UINT32(1, sink.attempts[recoveryStart].launchId);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[recoveryStart + 1][1]);
    TEST_ASSERT_EQUAL_UINT32(1, sink.attempts[recoveryStart + 1].launchId);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[recoveryStart + 2][1]);
    TEST_ASSERT_EQUAL_UINT32(2, sink.attempts[recoveryStart + 2].launchId);
    TEST_ASSERT_EQUAL_UINT32(2, sequencer.actualSoundingLaunch()->launchId);

    for (std::uint32_t tick = 7; tick < 12; ++tick) {
        dispatcher.consumeTick({1'000 + tick * 20'000, 20'000}, false, 1'000 + tick * 20'000);
    }
    TEST_ASSERT_EQUAL_UINT32(recoveryStart + 3, sink.count);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.staleGateOffCount);
}

void test_retry_window_equality_causes_one_controlled_stop() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    sink.results.fill(SwingMetro::SendResult::RetryLater);
    sink.results[0] = SwingMetro::SendResult::Accepted;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;
    controller.applyMode(SwingMetro::MidiClockMode::Internal, 0);
    controller.toggle(1);
    for (std::uint32_t tick = 0; tick <= SwingMetro::TICKS_PER_SIXTEENTH; ++tick) {
        const auto atUs = 1'000 + tick * 20'000;
        TEST_ASSERT_TRUE(ticks.publish({atUs, 20'000}));
        controller.process(atUs, ticks);
    }
    controller.process(121'001, ticks);

    TEST_ASSERT_FALSE(controller.usesInternalTiming());
    TEST_ASSERT_EQUAL(
        static_cast<std::uint8_t>(SwingMetro::InvalidationReason::RetryWindowExceeded),
        static_cast<std::uint8_t>(controller.diagnostics().lastSessionEndReason));
}

void test_repeated_stop_retries_one_off_before_one_stop() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    dispatcher.start(true, 0);
    sequencer.notifyNoteOnAccepted(60);
    sink.results[1] = SwingMetro::SendResult::RetryLater;
    sink.results[2] = SwingMetro::SendResult::Accepted;
    sink.results[3] = SwingMetro::SendResult::RetryLater;
    sink.results[4] = SwingMetro::SendResult::Accepted;

    dispatcher.stop(sequencer.stop(), 100);
    dispatcher.stop(sequencer.stop(), 101);
    dispatcher.beginPass(102);
    dispatcher.finishPass(102);
    dispatcher.beginPass(103);
    dispatcher.finishPass(103);

    TEST_ASSERT_EQUAL_UINT32(5, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[1][1]);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.packets[2][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.packets[3][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.packets[4][1]);
    TEST_ASSERT_FALSE(dispatcher.pendingFront().has_value());
}

void test_stop_does_not_duplicate_an_already_pending_note_off() {
    Sequencer sequencer;
    sequencer.notifyNoteOnAccepted(60);
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results[0] = SwingMetro::SendResult::RetryLater;
    sink.results[1] = SwingMetro::SendResult::Accepted;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, 0}, *SwingMetro::MidiMessage::noteOff(0, 60)));
    dispatcher.start(false, 0);
    dispatcher.beginPass(1'000);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    dispatcher.finishPass(1'000);
    dispatcher.stop(sequencer.stop(), 1'001);
    dispatcher.beginPass(1'002);
    dispatcher.finishPass(1'002);

    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_FALSE(dispatcher.pendingFront().has_value());
}

void test_new_start_supersedes_unaccepted_old_stop() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    sink.results[1] = SwingMetro::SendResult::RetryLater;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    dispatcher.start(true, 0);
    dispatcher.stop(std::nullopt, 100);
    dispatcher.start(true, 101);

    TEST_ASSERT_EQUAL_UINT32(3, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.packets[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.packets[1][1]);
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.packets[2][1]);
    TEST_ASSERT_FALSE(dispatcher.pendingFront().has_value());
}

void test_disconnect_requires_explicit_start_and_never_replays() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    sink.results[0] = SwingMetro::SendResult::Disconnected;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    controller.applyMode(SwingMetro::MidiClockMode::External, 0);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 1);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 1'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 21'833);
    TEST_ASSERT_EQUAL_UINT32(1, sink.count);
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(RemoteNoteState::Clean),
                      static_cast<std::uint8_t>(sequencer.remoteNoteState()));

    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Continue, 30'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 31'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 51'833);
    TEST_ASSERT_EQUAL_UINT32(1, sink.count);

    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 60'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 61'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 81'833);
    TEST_ASSERT_EQUAL_UINT32(2, sink.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, sink.packets[1][1]);
    for (const auto messageClass :
         {SwingMetro::MidiMessageClass::Clock, SwingMetro::MidiMessageClass::Transport,
          SwingMetro::MidiMessageClass::Note}) {
        assertDeliveryBalances(controller.diagnostics(), messageClass);
    }
}

void test_disconnect_abandons_terminal_off_and_marks_remote_unknown() {
    Sequencer sequencer;
    enableFirstStep(sequencer);
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    sink.results[0] = SwingMetro::SendResult::Accepted;
    sink.results[1] = SwingMetro::SendResult::Disconnected;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    controller.applyMode(SwingMetro::MidiClockMode::External, 0);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 1);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 1'000);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 21'833);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Stop, 22'000);

    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(RemoteNoteState::Unknown),
                      static_cast<std::uint8_t>(sequencer.remoteNoteState()));
    TEST_ASSERT_EQUAL_UINT32(1, controller.diagnostics().terminalNoteOffAbandonedCount);
    TEST_ASSERT_EQUAL_UINT32(0, controller.diagnostics().currentOutboxDepth);
    TEST_ASSERT_EQUAL_UINT32(
        1,
        controller.diagnostics().pendingRemovals[static_cast<std::size_t>(
            SwingMetro::DeliveryRemovalReason::Disconnected)]
                                                [classIndex(SwingMetro::MidiMessageClass::Note)]);
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(SwingMetro::InvalidationReason::Disconnected),
                      static_cast<std::uint8_t>(controller.diagnostics().lastSessionEndReason));
}

void test_mode_storage_and_external_loss_record_distinct_reasons() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink sink;
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;
    controller.applyMode(SwingMetro::MidiClockMode::Internal, 1);
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(SwingMetro::InvalidationReason::ModeSwitch),
                      static_cast<std::uint8_t>(controller.diagnostics().lastSessionEndReason));
    controller.toggle(2);
    controller.openStorage(3);
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(SwingMetro::InvalidationReason::Storage),
                      static_cast<std::uint8_t>(controller.diagnostics().lastSessionEndReason));
    controller.closeStorage();
    controller.applyMode(SwingMetro::MidiClockMode::External, 4);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Start, 5);
    handleExternal(controller, SwingMetro::MidiRealtimeEventType::Clock, 1'000);
    controller.process(1'000 + SwingMetro::ExternalMidiClock::CLOCK_LOSS_TIMEOUT_US, ticks);
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(SwingMetro::InvalidationReason::ExternalClockLost),
                      static_cast<std::uint8_t>(controller.diagnostics().lastSessionEndReason));
}

void test_delivery_diagnostics_preserve_retry_identity_and_lateness() {
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

    dispatcher.start(false, 0);
    dispatcher.beginPass(1'000);
    dispatcher.consumeTick({1'000, 20'000}, false, 1'000);
    dispatcher.finishPass(1'000);
    dispatcher.beginPass(1'025);
    dispatcher.finishPass(1'025);

    const auto note = static_cast<std::size_t>(SwingMetro::DeliveryMessageClass::Note);
    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.delivery[note].attempts);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.delivery[note].accepted);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.delivery[note].retryLater);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.delivery[note].disconnected);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.delivery[note].retryRecovered);
    TEST_ASSERT_EQUAL_UINT32(25, diagnostics.delivery[note].maxAcceptanceLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(0, sink.attempts[0].deliverySequenceNumber);
    TEST_ASSERT_EQUAL_UINT32(0, sink.attempts[1].deliverySequenceNumber);
    TEST_ASSERT_TRUE(sink.attempts[0].firstAttempt);
    TEST_ASSERT_FALSE(sink.attempts[1].firstAttempt);
    TEST_ASSERT_EQUAL_UINT8(1, sink.attempts[0].attemptOrdinal);
    TEST_ASSERT_EQUAL_UINT8(2, sink.attempts[1].attemptOrdinal);
    TEST_ASSERT_EQUAL_UINT32(diagnostics.delivery[note].attempts,
                             diagnostics.delivery[note].accepted +
                                 diagnostics.delivery[note].retryLater +
                                 diagnostics.delivery[note].disconnected);
    TEST_ASSERT_EQUAL_UINT32(diagnostics.outboxInserted[note],
                             diagnostics.delivery[note].accepted + diagnostics.currentOutboxDepth);
}

void test_delivery_diagnostics_classify_clock_transport_and_pass_limit() {
    Sequencer sequencer;
    SwingMetro::MidiEventQueue queue;
    SwingMetro::Transport transport;
    Sink sink;
    SwingMetro::TransportDiagnostics diagnostics;
    SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
    dispatcher.beginPass(1'000);
    dispatcher.start(true, 1'000);
    dispatcher.consumeTick({1'000, 20'000}, true, 1'000);
    dispatcher.finishPass(1'000);

    const auto clock = static_cast<std::size_t>(SwingMetro::DeliveryMessageClass::Clock);
    const auto transportClass =
        static_cast<std::size_t>(SwingMetro::DeliveryMessageClass::Transport);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.delivery[clock].accepted);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.delivery[transportClass].accepted);
    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.maxSendAttemptsPerPublicPass);
    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.currentSessionGeneration);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.explicitCleanStarts);
}

void test_delivery_result_equation_holds_for_each_message_class() {
    {
        Sequencer sequencer;
        SwingMetro::MidiEventQueue queue;
        SwingMetro::Transport transport;
        Sink sink;
        sink.results[0] = SwingMetro::SendResult::RetryLater;
        sink.results[1] = SwingMetro::SendResult::Accepted;
        SwingMetro::TransportDiagnostics diagnostics;
        SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
        dispatcher.start(false, 0);
        dispatcher.beginPass(1'000);
        dispatcher.consumeTick({1'000, 20'000}, true, 1'000);
        dispatcher.finishPass(1'000);
        dispatcher.beginPass(1'001);
        dispatcher.finishPass(1'001);
        const auto messageClass = static_cast<std::size_t>(SwingMetro::DeliveryMessageClass::Clock);
        TEST_ASSERT_EQUAL_UINT32(diagnostics.delivery[messageClass].attempts,
                                 diagnostics.delivery[messageClass].accepted +
                                     diagnostics.delivery[messageClass].retryLater +
                                     diagnostics.delivery[messageClass].disconnected);
    }
    {
        Sequencer sequencer;
        SwingMetro::MidiEventQueue queue;
        SwingMetro::Transport transport;
        Sink sink;
        sink.results[0] = SwingMetro::SendResult::Disconnected;
        SwingMetro::TransportDiagnostics diagnostics;
        SwingMetro::MidiDispatcher dispatcher{queue, transport, sequencer, sink, diagnostics};
        dispatcher.start(true, 0);
        const auto messageClass =
            static_cast<std::size_t>(SwingMetro::DeliveryMessageClass::Transport);
        TEST_ASSERT_EQUAL_UINT32(1, diagnostics.delivery[messageClass].disconnected);
        TEST_ASSERT_EQUAL_UINT32(diagnostics.delivery[messageClass].attempts,
                                 diagnostics.delivery[messageClass].accepted +
                                     diagnostics.delivery[messageClass].retryLater +
                                     diagnostics.delivery[messageClass].disconnected);
    }
}

void test_fault_sink_retry_first_clock_preserves_start_and_recovers() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink delegate;
    SwingMetro::FaultMidiMessageSink sink{delegate};
    sink.select(SwingMetro::FaultScenario::RetryFirstClock);
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal, 0);
    controller.toggle(1);
    TEST_ASSERT_TRUE(controller.usesInternalTiming());
    TEST_ASSERT_EQUAL(SwingMetro::MidiMessageClass::Transport,
                      sink.attempts[0].message.messageClass());
    TEST_ASSERT_EQUAL_UINT32(1, delegate.count);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    controller.process(1'000, ticks);
    TEST_ASSERT_TRUE(controller.usesInternalTiming());
    controller.process(1'001, ticks);

    const auto diagnostics = controller.diagnostics();
    const auto clock = classIndex(SwingMetro::MidiMessageClass::Clock);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.delivery[clock].retryLater);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.delivery[clock].retryRecovered);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.delivery[clock].accepted);
    TEST_ASSERT_EQUAL_UINT32(2, delegate.count);
}

void test_fault_sink_sustained_backpressure_stops_at_existing_retry_window() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink delegate;
    SwingMetro::FaultMidiMessageSink sink{delegate};
    sink.select(SwingMetro::FaultScenario::SustainedBackpressure);
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal, 0);
    controller.toggle(1);
    for (std::uint32_t tick = 0; tick <= SwingMetro::TICKS_PER_SIXTEENTH; ++tick) {
        const auto atUs = 1'000 + tick * 20'000;
        TEST_ASSERT_TRUE(ticks.publish({atUs, 20'000}));
        controller.process(atUs, ticks);
    }
    controller.process(121'001, ticks);

    TEST_ASSERT_FALSE(controller.usesInternalTiming());
    TEST_ASSERT_EQUAL_UINT32(1, delegate.count);
    TEST_ASSERT_EQUAL_UINT32(1, controller.diagnostics().retryWindowSafetyStops);
}

void test_fault_sink_disconnect_requires_new_start() {
    Sequencer sequencer;
    SwingMetro::MidiClockSettings settings;
    Sink delegate;
    SwingMetro::FaultMidiMessageSink sink{delegate};
    sink.select(SwingMetro::FaultScenario::DeterministicDisconnect);
    SwingMetro::TransportController controller{sequencer, settings, sink};
    SwingMetro::InternalTickStore<> ticks;

    controller.applyMode(SwingMetro::MidiClockMode::Internal, 0);
    controller.toggle(1);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    controller.process(1'000, ticks);
    TEST_ASSERT_FALSE(controller.isRunning());
    TEST_ASSERT_FALSE(controller.usesInternalTiming());
    TEST_ASSERT_EQUAL_UINT32(1, delegate.count);
    const auto clock = classIndex(SwingMetro::MidiMessageClass::Clock);
    TEST_ASSERT_EQUAL_UINT32(1, controller.diagnostics().delivery[clock].disconnected);
    TEST_ASSERT_EQUAL_UINT32(1, controller.diagnostics().sessionEnds[static_cast<std::size_t>(
                                    SwingMetro::InvalidationReason::Disconnected)]);

    controller.toggle(2'000);
    TEST_ASSERT_TRUE(controller.usesInternalTiming());
}

void test_fault_sink_baseline_delegates_all_result_classes() {
    Sink delegate;
    delegate.results[0] = SwingMetro::SendResult::RetryLater;
    SwingMetro::FaultMidiMessageSink sink{delegate};
    const SwingMetro::MidiDeliveryAttempt start{SwingMetro::MidiMessage::start()};
    const SwingMetro::MidiDeliveryAttempt clock{SwingMetro::MidiMessage::clock()};
    const SwingMetro::MidiDeliveryAttempt note{noteOn(0, 60, 100)};

    TEST_ASSERT_EQUAL(SwingMetro::SendResult::RetryLater, sink.send(start));
    TEST_ASSERT_EQUAL(SwingMetro::SendResult::Accepted, sink.send(clock));
    TEST_ASSERT_EQUAL(SwingMetro::SendResult::Accepted, sink.send(note));
    TEST_ASSERT_EQUAL_UINT32(3, delegate.count);
    TEST_ASSERT_EQUAL_UINT32(3, sink.attemptCount());
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
    RUN_TEST(test_lifecycle_expiry_and_generation_helpers_are_exact_and_saturating);
    RUN_TEST(test_lateness_distributions_cover_edges_wrap_and_message_classes);
    RUN_TEST(test_lateness_distributions_count_retry_once_per_attempt_and_acceptance);
    RUN_TEST(test_scheduled_depth_tracks_current_and_high_water);
    RUN_TEST(test_clock_backpressure_coalesces_to_latest_without_burst);
    RUN_TEST(test_stale_clock_expires_instead_of_replaying);
    RUN_TEST(test_note_on_expires_at_retry_window_equality_without_acceptance);
    RUN_TEST(test_overlapping_same_pitch_replaces_with_identity_safe_off);
    RUN_TEST(test_retrying_swung_projection_gets_early_off_before_same_pitch_replacement);
    RUN_TEST(test_retry_window_equality_causes_one_controlled_stop);
    RUN_TEST(test_repeated_stop_retries_one_off_before_one_stop);
    RUN_TEST(test_stop_does_not_duplicate_an_already_pending_note_off);
    RUN_TEST(test_new_start_supersedes_unaccepted_old_stop);
    RUN_TEST(test_disconnect_requires_explicit_start_and_never_replays);
    RUN_TEST(test_disconnect_abandons_terminal_off_and_marks_remote_unknown);
    RUN_TEST(test_mode_storage_and_external_loss_record_distinct_reasons);
    RUN_TEST(test_delivery_diagnostics_preserve_retry_identity_and_lateness);
    RUN_TEST(test_delivery_diagnostics_classify_clock_transport_and_pass_limit);
    RUN_TEST(test_delivery_result_equation_holds_for_each_message_class);
    RUN_TEST(test_fault_sink_retry_first_clock_preserves_start_and_recovers);
    RUN_TEST(test_fault_sink_sustained_backpressure_stops_at_existing_retry_window);
    RUN_TEST(test_fault_sink_disconnect_requires_new_start);
    RUN_TEST(test_fault_sink_baseline_delegates_all_result_classes);
    RUN_TEST(test_process_duration_is_recorded_separately_from_service_interval);
}
