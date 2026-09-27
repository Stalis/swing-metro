#include "diagnostics_serializer.h"

namespace SwingMetro {
namespace {
constexpr std::array<const char*, SwingMetro::DELIVERY_MESSAGE_CLASS_COUNT> DELIVERY_CLASS_NAMES = {
    "clock", "transport", "note"};
constexpr std::array<const char*,
                     static_cast<std::size_t>(SwingMetro::InvalidationReason::SupersededStart) + 1>
    INVALIDATION_REASON_NAMES = {"stop",
                                 "mode_switch",
                                 "storage",
                                 "external_clock_lost",
                                 "retry_window_exceeded",
                                 "delivery_capacity",
                                 "disconnected",
                                 "superseded_start"};
constexpr std::array<const char*, SwingMetro::DELIVERY_REMOVAL_REASON_COUNT>
    DELIVERY_REMOVAL_NAMES = {
        "clock_coalesced",   "clock_expired", "note_on_expired",     "stop",
        "mode_switch",       "storage",       "external_clock_lost", "retry_window_exceeded",
        "delivery_capacity", "disconnected",  "superseded_start",    "scheduled_overdue",
        "stale_gate_off"};
constexpr std::array<const char*, 4> LATENESS_DISTRIBUTION_NAMES = {
    "clock_attempt", "clock_accepted", "note_attempt", "note_accepted"};
constexpr std::array<const char*, SwingMetro::LatenessDistribution::POSITIVE_BUCKET_COUNT>
    LATENESS_BUCKET_NAMES = {"1_10",     "11_50",     "51_100",     "101_250",      "251_500",
                             "501_1000", "1001_5000", "5001_20000", "20001_100000", "ge_100001"};

} // namespace

void DiagnosticsSerializer::printDeliveryDiagnosticsHeader() {
    for (const auto* messageClass : DELIVERY_CLASS_NAMES) {
        _output.print(',');
        _output.print("delivery_");
        _output.print(messageClass);
        _output.print("_attempts,delivery_");
        _output.print(messageClass);
        _output.print("_accepted,delivery_");
        _output.print(messageClass);
        _output.print("_retry_later,delivery_");
        _output.print(messageClass);
        _output.print("_disconnected,delivery_");
        _output.print(messageClass);
        _output.print("_retry_recovered,delivery_");
        _output.print(messageClass);
        _output.print("_max_first_attempt_lateness_us,delivery_");
        _output.print(messageClass);
        _output.print("_max_acceptance_lateness_us");
    }
    for (const auto* reason : DELIVERY_REMOVAL_NAMES) {
        for (const auto* messageClass : DELIVERY_CLASS_NAMES) {
            _output.print(",pending_removed_");
            _output.print(reason);
            _output.print('_');
            _output.print(messageClass);
            _output.print(",scheduled_removed_");
            _output.print(reason);
            _output.print('_');
            _output.print(messageClass);
        }
    }
    for (const auto* messageClass : DELIVERY_CLASS_NAMES) {
        _output.print(",scheduled_created_");
        _output.print(messageClass);
        _output.print(",scheduled_transferred_");
        _output.print(messageClass);
        _output.print(",outbox_inserted_");
        _output.print(messageClass);
        _output.print(",current_scheduled_depth_");
        _output.print(messageClass);
        _output.print(",current_outbox_depth_");
        _output.print(messageClass);
    }
    _output.print(",clock_coalesced_count,clock_expired_count,note_on_expired_count,"
                  "terminal_note_off_abandoned_count,terminal_stop_abandoned_count,"
                  "current_outbox_depth,max_outbox_depth,max_send_attempts_per_public_pass,"
                  "outbox_capacity_failures,delivery_capacity_safety_stops,"
                  "retry_window_safety_stops,session_generation_advances,explicit_clean_starts,"
                  "current_session_generation");
    for (const auto* reason : INVALIDATION_REASON_NAMES) {
        _output.print(",session_ends_");
        _output.print(reason);
    }
    _output.print(",current_scheduled_depth_total,max_scheduled_depth");
    for (const auto* distribution : LATENESS_DISTRIBUTION_NAMES) {
        _output.print(',');
        _output.print(distribution);
        _output.print("_lateness_early,");
        _output.print(distribution);
        _output.print("_lateness_on_time,");
        _output.print(distribution);
        _output.print("_lateness_unordered");
        for (const auto* bucket : LATENESS_BUCKET_NAMES) {
            _output.print(',');
            _output.print(distribution);
            _output.print("_lateness_");
            _output.print(bucket);
        }
    }
    _output.print(",observed_internal_tick_queue_depth,"
                  "observed_internal_tick_queue_high_water,internal_tick_queue_overflows");
}

void DiagnosticsSerializer::printDeliveryDiagnostics(
    const SwingMetro::TransportDiagnostics& transport,
    const SwingMetro::InternalTickDiagnostics& producer) {
    const auto print = [this](std::uint32_t value) { _output.print(value); };
    for (const auto& delivery : transport.delivery) {
        _output.print(',');
        print(delivery.attempts);
        _output.print(',');
        print(delivery.accepted);
        _output.print(',');
        print(delivery.retryLater);
        _output.print(',');
        print(delivery.disconnected);
        _output.print(',');
        print(delivery.retryRecovered);
        _output.print(',');
        print(delivery.maxFirstAttemptLatenessUs);
        _output.print(',');
        print(delivery.maxAcceptanceLatenessUs);
    }
    for (std::size_t reason = 0; reason < DELIVERY_REMOVAL_NAMES.size(); ++reason) {
        for (std::size_t messageClass = 0; messageClass < DELIVERY_CLASS_NAMES.size();
             ++messageClass) {
            _output.print(',');
            print(transport.pendingRemovals[reason][messageClass]);
            _output.print(',');
            print(transport.scheduledRemovals[reason][messageClass]);
        }
    }
    for (std::size_t messageClass = 0; messageClass < DELIVERY_CLASS_NAMES.size(); ++messageClass) {
        _output.print(',');
        print(transport.scheduledCreated[messageClass]);
        _output.print(',');
        print(transport.scheduledTransferred[messageClass]);
        _output.print(',');
        print(transport.outboxInserted[messageClass]);
        _output.print(',');
        print(transport.currentScheduledDepth[messageClass]);
        _output.print(',');
        print(transport.currentOutboxDepthByClass[messageClass]);
    }
    _output.print(',');
    print(transport.clockCoalescedCount);
    _output.print(',');
    print(transport.clockExpiredCount);
    _output.print(',');
    print(transport.noteOnExpiredCount);
    _output.print(',');
    print(transport.terminalNoteOffAbandonedCount);
    _output.print(',');
    print(transport.terminalStopAbandonedCount);
    _output.print(',');
    print(transport.currentOutboxDepth);
    _output.print(',');
    print(transport.maxOutboxDepth);
    _output.print(',');
    print(transport.maxSendAttemptsPerPublicPass);
    _output.print(',');
    print(transport.outboxCapacityFailures);
    _output.print(',');
    print(transport.deliveryCapacitySafetyStops);
    _output.print(',');
    print(transport.retryWindowSafetyStops);
    _output.print(',');
    print(transport.sessionGenerationAdvances);
    _output.print(',');
    print(transport.explicitCleanStarts);
    _output.print(',');
    print(transport.currentSessionGeneration);
    for (const auto count : transport.sessionEnds) {
        _output.print(',');
        print(count);
    }
#if SWING_METRO_STAGE5_INSTRUMENTATION
    _output.print(',');
    print(transport.currentScheduledDepthTotal);
    _output.print(',');
    print(transport.maxScheduledDepth);
    const auto printDistribution = [this,
                                    &print](const SwingMetro::LatenessDistribution& distribution) {
        _output.print(',');
        print(distribution.early);
        _output.print(',');
        print(distribution.onTime);
        _output.print(',');
        print(distribution.unordered);
        for (const auto bucket : distribution.positive) {
            _output.print(',');
            print(bucket);
        }
    };
    printDistribution(transport.clockAttemptLateness);
    printDistribution(transport.clockAcceptedLateness);
    printDistribution(transport.noteAttemptLateness);
    printDistribution(transport.noteAcceptedLateness);
#else
    constexpr std::size_t stage5TransportFieldCount =
        2 + (4 * (3 + SwingMetro::LatenessDistribution::POSITIVE_BUCKET_COUNT));
    for (std::size_t field = 0; field < stage5TransportFieldCount; ++field) {
        _output.print(",0");
    }
#endif
    _output.print(',');
    print(producer.observedTickQueueDepth);
    _output.print(',');
    print(producer.observedTickQueueHighWater);
    _output.print(',');
    print(producer.tickQueueOverflows);
}

void DiagnosticsSerializer::exportInternalTimingDiagnostics(
    const TickPipelineDiagnostics& diagnostics, const TransportDiagnostics& transport,
    const RuntimeTimingSnapshot& runtime, const EncoderSampleWindowDiagnostics& encoderWindow,
    const EncoderSampleDiagnostics& encoderSampleDiagnostics) {
    const auto& producer = diagnostics.producer;
    if (!_headerPrinted) {
        _output.print(
            "swing_metro_diagnostics_v4,alarm_callback_invocations,"
            "synchronous_start_publication_attempts,successful_publications,"
            "failed_publications,tick_queue_overflows,stop_discards,mode_switch_discards,"
            "storage_discards,alarm_arm_failures,stale_alarm_callbacks,"
            "stale_alarm_arm_failures,missed_scheduled_targets,"
            "out_of_horizon_alarm_callbacks,max_actual_callback_interval_us,"
            "max_callback_lateness_us,successful_consumer_pops,budget_discards,"
            "outgoing_internal_f8_attempts,max_service_interval_us,"
            "max_internal_tick_processing_lateness_us,"
            "max_external_tick_processing_lateness_us,max_f8_attempt_lateness_us,"
            "max_queued_event_attempt_lateness_us,max_internal_ticks_popped_per_process_pass,"
            "internal_tick_budget_reached_passes,"
            "max_remaining_internal_ticks_after_budget_pass,max_process_duration_us");
        printDeliveryDiagnosticsHeader();
        _output.println();
        _output.println("swing_metro_input_diagnostics_v1,max_actual_encoder_sample_interval_us,"
                        "encoder_sample_intervals_above_1250_us");
        _output.println("swing_metro_runtime_diagnostics_v1,lv_timer_handler_count,"
                        "lv_timer_handler_inclusive_total_us,lv_timer_handler_inclusive_max_us,"
                        "display_flush_count,display_flush_inclusive_total_us,"
                        "display_flush_inclusive_max_us,encoder_sample_window_max_interval_us,"
                        "encoder_sample_window_intervals_above_1250_us");
        _headerPrinted = true;
    }

    _output.print("swing_metro_diagnostics_v4,");
    const auto print = [this](std::uint32_t value) { _output.print(value); };
    const auto separator = [this]() { _output.print(','); };
    print(producer.alarmCallbackInvocations);
    separator();
    print(producer.synchronousStartPublicationAttempts);
    separator();
    print(producer.successfulPublications);
    separator();
    print(producer.failedPublications);
    separator();
    print(producer.failedPublications);
    separator();
    print(producer.stopDiscards);
    separator();
    print(producer.modeSwitchDiscards);
    separator();
    print(producer.storageDiscards);
    separator();
    print(producer.alarmArmFailures);
    separator();
    print(producer.staleAlarmCallbacks);
    separator();
    print(producer.staleAlarmArmFailures);
    separator();
    print(producer.missedScheduledTargets);
    separator();
    print(producer.outOfHorizonAlarmCallbacks);
    separator();
    print(producer.maxActualCallbackIntervalUs);
    separator();
    print(producer.maxCallbackLatenessUs);
    separator();
    print(diagnostics.successfulConsumerPops);
    separator();
    print(diagnostics.budgetDiscards);
    separator();
    print(diagnostics.outgoingInternalClockAttempts);
    separator();
    print(transport.maxServiceIntervalUs);
    separator();
    print(transport.maxInternalTickProcessingLatenessUs);
    separator();
    print(transport.maxExternalTickProcessingLatenessUs);
    separator();
    print(transport.maxClockAttemptLatenessUs);
    separator();
    print(transport.maxQueuedEventAttemptLatenessUs);
    separator();
    print(transport.maxInternalTicksPoppedPerProcessPass);
    separator();
    print(transport.internalTickBudgetReachedPasses);
    separator();
    print(transport.maxRemainingInternalTicksAfterBudgetPass);
    separator();
    print(transport.maxProcessDurationUs);
    printDeliveryDiagnostics(transport, producer);
    _output.println();
    _output.print("swing_metro_input_diagnostics_v1,");
    _output.print(encoderSampleDiagnostics.maxActualIntervalUs());
    _output.print(',');
    _output.println(encoderSampleDiagnostics.intervalsAboveBound());
    _output.print("swing_metro_runtime_diagnostics_v1,");
    print(runtime.lvTimerHandlerCount);
    separator();
    print(runtime.lvTimerHandlerInclusiveTotalUs);
    separator();
    print(runtime.lvTimerHandlerInclusiveMaxUs);
    separator();
    print(runtime.displayFlushCount);
    separator();
    print(runtime.displayFlushInclusiveTotalUs);
    separator();
    print(runtime.displayFlushInclusiveMaxUs);
    separator();
    print(encoderWindow.maxActualIntervalUs);
    separator();
    print(encoderWindow.intervalsAboveBound);
    _output.println();
}

} // namespace SwingMetro
