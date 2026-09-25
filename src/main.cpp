#include "components/ui_view_model.h"
#include "drivers/littlefs_program_storage.h"
#include "drivers/lvgl_ui.h"
#include "drivers/pico_internal_tick_alarm.h"
#include "drivers/usb_midi_adapter.h"
#include "engine/runtime_timing_diagnostics.h"
#include "engine/transport_controller.h"
#include "input/app_event_handler.h"
#include "input/app_input.h"
#include "input/app_input_coordinator.h"
#include "input/encoder_sample_diagnostics.h"
#include "input/pad_button_ids.h"
#include "input/periodic_scheduler.h"
#include "input/serial_run_command.h"
#include "input/step_button_inputs.h"
#include "program/program_slot_store.h"
#include "program/program_storage_controller.h"
#include <Arduino.h>
#include <adapters/button_input.h>
#include <adapters/encoder_input.h>
#include <adapters/trigger_input.h>
#include <array>
#include <button_matrix.h>
#include <context_input.h>
#include <cstddef>
#include <encoder.h>
#include <tuple>
#include <variant>

#include "engine/sequencer.h"
#include <Adafruit_TinyUSB.h>
#include <utils/counter.h>

Adafruit_USBD_MIDI usbMidi;
SwingMetro::UsbMidiRealtimeReceiver midiClockReceiver{usbMidi};

Sequencer mainSequencer;
SwingMetro::MidiClockSettings midiClockSettings;
SwingMetro::UsbMidiMessageSink midiSink{usbMidi};
SwingMetro::InternalTickSource internalTicks;
SwingMetro::PicoInternalTickAlarm internalTickAlarm{internalTicks};
SwingMetro::TransportController transportController{mainSequencer, midiClockSettings, midiSink};
SwingMetro::LittleFsProgramStorage programStorageBackend;
SwingMetro::ProgramSlotStore programSlotStore{programStorageBackend};

// Room for global controls, the base screen, settings, and temporary overlays.
constexpr std::size_t INPUT_CONTEXT_CAPACITY = 8;
ContextInput::EncoderInputAdapter<SwingMetro::InputId> tempoInput{
    SwingMetro::InputId::TempoEncoder,
};
ContextInput::EncoderInputAdapter<SwingMetro::InputId> swingInput{
    SwingMetro::InputId::SwingEncoder,
};
ContextInput::EncoderInputAdapter<SwingMetro::InputId> volumeInput{
    SwingMetro::InputId::VolumeEncoder,
};

constexpr std::array<uint8_t, 4> INPUT_PINS = {D0, D1, D2, D3};
constexpr std::array<uint8_t, 4> OUTPUT_PINS = {D4, D5, D6, D7};
constexpr std::uint32_t MATRIX_SCAN_PERIOD_MS = 5;
constexpr std::uint32_t ENCODER_SAMPLE_PERIOD_US = 1'000;
ButtonMatrix<4, 4, SwingMetro::PadButtonIds> buttonMatrix(INPUT_PINS, OUTPUT_PINS);
SwingMetro::StepButtonInputs stepButtonInputs;
SwingMetro::PeriodicScheduler<MATRIX_SCAN_PERIOD_MS> matrixScanScheduler;
SwingMetro::PeriodicScheduler<ENCODER_SAMPLE_PERIOD_US> encoderSampleScheduler;
SwingMetro::EncoderSampleDiagnostics encoderSampleDiagnostics;
SwingMetro::RuntimeTimingDiagnostics runtimeTimingDiagnostics;
SwingMetro::EncoderSampleWindowDiagnostics runtimeEncoderSnapshot;
std::uint32_t runtimeDiagnosticsRequestGeneration = 0;
bool runtimeDiagnosticsExportPending = false;

void tempoEncoderHandler(EncoderDirection direction);
void tempoEncoderSwitchHandler(std::uint32_t nowUs);
void tempoEncoderSwitchReleaseHandler(std::uint32_t nowUs);
constexpr EncoderSettings TEMPO_ENCODER_SETTINGS{
    .pinA = 16,
    .pinB = 17,
    .pinSwitch = 18,
    .handler = tempoEncoderHandler,
    .switchHandler = tempoEncoderSwitchHandler,
    .switchReleaseHandler = tempoEncoderSwitchReleaseHandler,
};
Encoder tempoEncoder(TEMPO_ENCODER_SETTINGS);
Counter<uint8_t> tempoCounter({.step = 1,
                               .value = 120,
                               .minValue = 40,
                               .maxValue = 240,
                               .overflowBehavior = CounterOverflowBehavior::Clamp});

void swingEncoderHandler(EncoderDirection direction);
constexpr EncoderSettings SWING_ENCODER_SETTINGS{
    .pinA = 19,
    .pinB = 20,
    .pinSwitch = 21,
    .handler = swingEncoderHandler,
};
Encoder swingEncoder(SWING_ENCODER_SETTINGS);
Counter<uint8_t> swingCounter({.step = 1,
                               .value = 50,
                               .minValue = 50,
                               .maxValue = SwingMetro::SWING_MAX_VALUE,
                               .overflowBehavior = CounterOverflowBehavior::Clamp});

void volumeEncoderHandler(EncoderDirection direction);
void volumeEncoderSwitchHandler(std::uint32_t nowUs);
void volumeEncoderSwitchReleaseHandler(std::uint32_t nowUs);
constexpr EncoderSettings VOLUME_ENCODER_SETTINGS{
    .pinA = 22,
    .pinB = 26,
    .pinSwitch = 27,
    .handler = volumeEncoderHandler,
    .switchHandler = volumeEncoderSwitchHandler,
    .switchReleaseHandler = volumeEncoderSwitchReleaseHandler,
};
Encoder volumeEncoder(VOLUME_ENCODER_SETTINGS);
Counter<uint8_t> volumeCounter({.step = 1,
                                .value = 100,
                                .minValue = 0,
                                .maxValue = 100,
                                .overflowBehavior = CounterOverflowBehavior::Clamp});

SwingMetro::AppEventHandler appEventHandler{{
    .tempo = tempoCounter,
    .swing = swingCounter,
    .volume = volumeCounter,
    .sequencer = mainSequencer,
}};
SwingMetro::ProgramStorageController programStorageController{
    programSlotStore, tempoCounter, swingCounter, volumeCounter, mainSequencer, midiClockSettings};
SwingMetro::AppInputCoordinator<INPUT_CONTEXT_CAPACITY> appInputCoordinator{
    appEventHandler, mainSequencer, midiClockSettings, &transportController,
    &programStorageController};
ContextInput::ButtonInputAdapter<SwingMetro::InputId> tempoSwitchInput{{
    SwingMetro::InputId::TempoSwitch,
    500'000,
}};
ContextInput::ButtonInputAdapter<SwingMetro::InputId> volumeSwitchInput{{
    SwingMetro::InputId::VolumeEncoder,
    500'000,
}};

constexpr const auto UPDATABLES = std::tie(tempoEncoder, swingEncoder, volumeEncoder);
UiViewModel uiViewModel;
bool internalAlarmActive = false;
uint8_t internalAlarmBpm = 0;
bool diagnosticsHeaderPrinted = false;
SwingMetro::SerialRunCommandParser serialRunCommandParser;
bool serialRunActive = false;
bool serialRunCompletionPending = false;
std::uint32_t serialRunStartedAtMs = 0;
std::uint32_t serialRunDurationMs = 0;

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

void printDeliveryDiagnosticsHeader() {
    for (const auto* messageClass : DELIVERY_CLASS_NAMES) {
        Serial.print(',');
        Serial.print(F("delivery_"));
        Serial.print(messageClass);
        Serial.print(F("_attempts,delivery_"));
        Serial.print(messageClass);
        Serial.print(F("_accepted,delivery_"));
        Serial.print(messageClass);
        Serial.print(F("_retry_later,delivery_"));
        Serial.print(messageClass);
        Serial.print(F("_disconnected,delivery_"));
        Serial.print(messageClass);
        Serial.print(F("_retry_recovered,delivery_"));
        Serial.print(messageClass);
        Serial.print(F("_max_first_attempt_lateness_us,delivery_"));
        Serial.print(messageClass);
        Serial.print(F("_max_acceptance_lateness_us"));
    }
    for (const auto* reason : DELIVERY_REMOVAL_NAMES) {
        for (const auto* messageClass : DELIVERY_CLASS_NAMES) {
            Serial.print(F(",pending_removed_"));
            Serial.print(reason);
            Serial.print('_');
            Serial.print(messageClass);
            Serial.print(F(",scheduled_removed_"));
            Serial.print(reason);
            Serial.print('_');
            Serial.print(messageClass);
        }
    }
    for (const auto* messageClass : DELIVERY_CLASS_NAMES) {
        Serial.print(F(",scheduled_created_"));
        Serial.print(messageClass);
        Serial.print(F(",scheduled_transferred_"));
        Serial.print(messageClass);
        Serial.print(F(",outbox_inserted_"));
        Serial.print(messageClass);
        Serial.print(F(",current_scheduled_depth_"));
        Serial.print(messageClass);
        Serial.print(F(",current_outbox_depth_"));
        Serial.print(messageClass);
    }
    Serial.print(F(",clock_coalesced_count,clock_expired_count,note_on_expired_count,"
                   "terminal_note_off_abandoned_count,terminal_stop_abandoned_count,"
                   "current_outbox_depth,max_outbox_depth,max_send_attempts_per_public_pass,"
                   "outbox_capacity_failures,delivery_capacity_safety_stops,"
                   "retry_window_safety_stops,session_generation_advances,explicit_clean_starts,"
                   "current_session_generation"));
    for (const auto* reason : INVALIDATION_REASON_NAMES) {
        Serial.print(F(",session_ends_"));
        Serial.print(reason);
    }
    Serial.print(F(",current_scheduled_depth_total,max_scheduled_depth"));
    for (const auto* distribution : LATENESS_DISTRIBUTION_NAMES) {
        Serial.print(',');
        Serial.print(distribution);
        Serial.print(F("_lateness_early,"));
        Serial.print(distribution);
        Serial.print(F("_lateness_on_time,"));
        Serial.print(distribution);
        Serial.print(F("_lateness_unordered"));
        for (const auto* bucket : LATENESS_BUCKET_NAMES) {
            Serial.print(',');
            Serial.print(distribution);
            Serial.print(F("_lateness_"));
            Serial.print(bucket);
        }
    }
    Serial.print(F(",observed_internal_tick_queue_depth,"
                   "observed_internal_tick_queue_high_water,internal_tick_queue_overflows"));
}

void printDeliveryDiagnostics(const SwingMetro::TransportDiagnostics& transport,
                              const SwingMetro::InternalTickDiagnostics& producer) {
    const auto print = [](std::uint32_t value) { Serial.print(value); };
    for (const auto& delivery : transport.delivery) {
        Serial.print(',');
        print(delivery.attempts);
        Serial.print(',');
        print(delivery.accepted);
        Serial.print(',');
        print(delivery.retryLater);
        Serial.print(',');
        print(delivery.disconnected);
        Serial.print(',');
        print(delivery.retryRecovered);
        Serial.print(',');
        print(delivery.maxFirstAttemptLatenessUs);
        Serial.print(',');
        print(delivery.maxAcceptanceLatenessUs);
    }
    for (std::size_t reason = 0; reason < DELIVERY_REMOVAL_NAMES.size(); ++reason) {
        for (std::size_t messageClass = 0; messageClass < DELIVERY_CLASS_NAMES.size();
             ++messageClass) {
            Serial.print(',');
            print(transport.pendingRemovals[reason][messageClass]);
            Serial.print(',');
            print(transport.scheduledRemovals[reason][messageClass]);
        }
    }
    for (std::size_t messageClass = 0; messageClass < DELIVERY_CLASS_NAMES.size(); ++messageClass) {
        Serial.print(',');
        print(transport.scheduledCreated[messageClass]);
        Serial.print(',');
        print(transport.scheduledTransferred[messageClass]);
        Serial.print(',');
        print(transport.outboxInserted[messageClass]);
        Serial.print(',');
        print(transport.currentScheduledDepth[messageClass]);
        Serial.print(',');
        print(transport.currentOutboxDepthByClass[messageClass]);
    }
    Serial.print(',');
    print(transport.clockCoalescedCount);
    Serial.print(',');
    print(transport.clockExpiredCount);
    Serial.print(',');
    print(transport.noteOnExpiredCount);
    Serial.print(',');
    print(transport.terminalNoteOffAbandonedCount);
    Serial.print(',');
    print(transport.terminalStopAbandonedCount);
    Serial.print(',');
    print(transport.currentOutboxDepth);
    Serial.print(',');
    print(transport.maxOutboxDepth);
    Serial.print(',');
    print(transport.maxSendAttemptsPerPublicPass);
    Serial.print(',');
    print(transport.outboxCapacityFailures);
    Serial.print(',');
    print(transport.deliveryCapacitySafetyStops);
    Serial.print(',');
    print(transport.retryWindowSafetyStops);
    Serial.print(',');
    print(transport.sessionGenerationAdvances);
    Serial.print(',');
    print(transport.explicitCleanStarts);
    Serial.print(',');
    print(transport.currentSessionGeneration);
    for (const auto count : transport.sessionEnds) {
        Serial.print(',');
        print(count);
    }
#if SWING_METRO_STAGE5_INSTRUMENTATION
    Serial.print(',');
    print(transport.currentScheduledDepthTotal);
    Serial.print(',');
    print(transport.maxScheduledDepth);
    const auto printDistribution = [&print](const SwingMetro::LatenessDistribution& distribution) {
        Serial.print(',');
        print(distribution.early);
        Serial.print(',');
        print(distribution.onTime);
        Serial.print(',');
        print(distribution.unordered);
        for (const auto bucket : distribution.positive) {
            Serial.print(',');
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
        Serial.print(F(",0"));
    }
#endif
    Serial.print(',');
    print(producer.observedTickQueueDepth);
    Serial.print(',');
    print(producer.observedTickQueueHighWater);
    Serial.print(',');
    print(producer.tickQueueOverflows);
}

void exportInternalTimingDiagnostics(
    const SwingMetro::RuntimeTimingSnapshot& runtime,
    const SwingMetro::EncoderSampleWindowDiagnostics& encoderWindow) {
    if (transportController.usesInternalTiming() || transportController.isRunning()) {
        return;
    }
    const auto diagnostics = transportController.pipelineDiagnostics(internalTicks);
    const auto& producer = diagnostics.producer;
    const auto transport = transportController.diagnostics();

    if (!diagnosticsHeaderPrinted) {
        Serial.print(
            F("swing_metro_diagnostics_v4,alarm_callback_invocations,"
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
              "max_remaining_internal_ticks_after_budget_pass,max_process_duration_us"));
        printDeliveryDiagnosticsHeader();
        Serial.println();
        Serial.println(F("swing_metro_input_diagnostics_v1,max_actual_encoder_sample_interval_us,"
                         "encoder_sample_intervals_above_1250_us"));
        Serial.println(F("swing_metro_runtime_diagnostics_v1,lv_timer_handler_count,"
                         "lv_timer_handler_inclusive_total_us,lv_timer_handler_inclusive_max_us,"
                         "display_flush_count,display_flush_inclusive_total_us,"
                         "display_flush_inclusive_max_us,encoder_sample_window_max_interval_us,"
                         "encoder_sample_window_intervals_above_1250_us"));
        diagnosticsHeaderPrinted = true;
    }

    Serial.print(F("swing_metro_diagnostics_v4,"));
    const auto print = [](std::uint32_t value) { Serial.print(value); };
    const auto separator = []() { Serial.print(','); };
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
    Serial.println();
    Serial.print(F("swing_metro_input_diagnostics_v1,"));
    Serial.print(encoderSampleDiagnostics.maxActualIntervalUs());
    Serial.print(',');
    Serial.println(encoderSampleDiagnostics.intervalsAboveBound());
    Serial.print(F("swing_metro_runtime_diagnostics_v1,"));
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
    Serial.println();
}

template <typename TAdapter>
void handleEncoderDirection(const TAdapter& adapter, EncoderDirection direction) {
    const auto input = adapter.translate(direction);
    if (!input.has_value()) {
        return;
    }

    appInputCoordinator.dispatch(*input, micros());
}

void handleProgramStorageEvent(const SwingMetro::AppEvent& event, std::uint32_t nowUs);

void pollSerialRunCommand() {
    if (serialRunActive || transportController.usesInternalTiming()) {
        return;
    }
    while (Serial.available() > 0) {
        const auto result = serialRunCommandParser.push(static_cast<char>(Serial.read()));
        if (result.status == SwingMetro::SerialRunCommandStatus::Pending) {
            continue;
        }
        if (result.status == SwingMetro::SerialRunCommandStatus::Invalid ||
            mainSequencer.isRunning()) {
            Serial.println(F("swing_metro_control_v1,error,expected RUN <ms> <bpm> <swing>"));
            continue;
        }

        tempoCounter.setValue(result.command.bpm);
        swingCounter.setValue(result.command.swing);
        mainSequencer.setBpm(tempoCounter.getValue());
        mainSequencer.setSwing(swingCounter.getValue());
        transportController.applyMode(SwingMetro::MidiClockMode::Internal, micros());
        Serial.print(F("swing_metro_control_v1,run_started,"));
        Serial.print(result.command.durationMs);
        Serial.print(',');
        Serial.print(result.command.bpm);
        Serial.print(',');
        Serial.println(result.command.swing);
        Serial.flush();
        serialRunStartedAtMs = millis();
        serialRunDurationMs = result.command.durationMs;
        serialRunActive = true;
        transportController.toggle(micros());
        return;
    }
}

void updateSerialRun() {
    if (!serialRunActive) {
        return;
    }
    if (!transportController.usesInternalTiming()) {
        serialRunActive = false;
        serialRunCompletionPending = true;
        return;
    }
    if (millis() - serialRunStartedAtMs >= serialRunDurationMs) {
        transportController.toggle(micros());
        serialRunActive = false;
        serialRunCompletionPending = true;
    }
}

void handleButtonBatch(const SwingMetro::StepButtonInputs::Batch& batch, std::uint32_t nowUs) {
    for (std::size_t index = 0; index < batch.size(); ++index) {
        const auto& input = batch[index];
        if (const auto event = appInputCoordinator.dispatch(input, nowUs); event.has_value()) {
            handleProgramStorageEvent(*event, nowUs);
        }
    }
}

void pollMatrixInputs(std::uint32_t nowMs) {
    const auto nowUs = nowMs * 1'000U;
    buttonMatrix.readButtons(nowMs);

    for (int physicalIndex = 0; physicalIndex < STEPS_COUNT; ++physicalIndex) {
        const auto step = buttonMatrix.getButtonId(physicalIndex);
        if (buttonMatrix.isButtonJustPressed(physicalIndex)) {
            handleButtonBatch(stepButtonInputs.onPressed(step, nowMs), nowUs);
        } else if (buttonMatrix.isButtonJustReleased(physicalIndex)) {
            handleButtonBatch(stepButtonInputs.onReleased(step, nowMs), nowUs);
        } else if (buttonMatrix.isButtonHolding(physicalIndex)) {
            handleButtonBatch(stepButtonInputs.update(step, nowMs), nowUs);
        }
    }

    buttonMatrix.update();
}

void pollEncoderInputs(std::uint32_t nowUs) {
    std::apply([nowUs](auto&... objects) { (objects.update(nowUs), ...); }, UPDATABLES);
    handleButtonBatch(tempoSwitchInput.update(nowUs), nowUs);
    handleButtonBatch(volumeSwitchInput.update(nowUs), nowUs);
}

void volumeEncoderHandler(EncoderDirection direction) {
    handleEncoderDirection(volumeInput, direction);
}

void swingEncoderHandler(EncoderDirection direction) {
    handleEncoderDirection(swingInput, direction);
}

void tempoEncoderHandler(EncoderDirection direction) {
    handleEncoderDirection(tempoInput, direction);
}

void tempoEncoderSwitchHandler(std::uint32_t nowUs) {
    handleButtonBatch(tempoSwitchInput.onPressed(nowUs), nowUs);
}

void tempoEncoderSwitchReleaseHandler(std::uint32_t nowUs) {
    handleButtonBatch(tempoSwitchInput.onReleased(nowUs), nowUs);
}

void volumeEncoderSwitchHandler(std::uint32_t nowUs) {
    handleButtonBatch(volumeSwitchInput.onPressed(nowUs), nowUs);
}

void volumeEncoderSwitchReleaseHandler(std::uint32_t nowUs) {
    handleButtonBatch(volumeSwitchInput.onReleased(nowUs), nowUs);
}

void handleProgramStorageEvent(const SwingMetro::AppEvent& event, std::uint32_t nowUs) {
    if (std::holds_alternative<SwingMetro::OpenProgramStorage>(event) &&
        appInputCoordinator.isProgramStorageModalOpen()) {
        transportController.openStorage(nowUs);
    } else if (std::holds_alternative<SwingMetro::CloseProgramStorage>(event)) {
        transportController.closeStorage();
    }
}

void syncInternalAlarm() {
    const bool shouldRun = transportController.usesInternalTiming();
    if (shouldRun && !internalAlarmActive) {
        (void)runtimeTimingDiagnostics.requestSnapshot();
        (void)encoderSampleDiagnostics.snapshotAndResetWindow();
        internalTickAlarm.start(mainSequencer.getBpm(),
                                transportController.internalTickDiscardReason());
        internalAlarmActive = true;
        internalAlarmBpm = mainSequencer.getBpm();
    } else if (!shouldRun && internalAlarmActive) {
        internalTickAlarm.stop(transportController.internalTickDiscardReason());
        internalAlarmActive = false;
        runtimeEncoderSnapshot = encoderSampleDiagnostics.snapshotAndResetWindow();
        runtimeDiagnosticsRequestGeneration = runtimeTimingDiagnostics.requestSnapshot();
        runtimeDiagnosticsExportPending = true;
    } else if (shouldRun && internalAlarmBpm != mainSequencer.getBpm()) {
        internalTickAlarm.setBpm(mainSequencer.getBpm());
        internalAlarmBpm = mainSequencer.getBpm();
    }
}

void setup() {

    Serial.begin(115200);

    // USB setup
    if (!TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.begin(0);
    }

    // MIDI setup
    usbMidi.setStringDescriptor("Swing Metro MIDI");
    usbMidi.begin();

    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(10);
        TinyUSBDevice.attach();
    }

    // Devices setup

    buttonMatrix.init();

    tempoEncoder.init();
    swingEncoder.init();
    volumeEncoder.init();
    mainSequencer.stop();
    (void)programSlotStore.mount();
    (void)programStorageController.restoreCurrentProgram();
    uiViewModel.publish(appInputCoordinator.decorateUiSettings(
        {tempoCounter.getValue(), swingCounter.getValue(), volumeCounter.getValue()}));

    mainSequencer.sync(micros());
    matrixScanScheduler.start(millis());
    encoderSampleScheduler.start(micros());
}

void loop() {
    pollSerialRunCommand();
    midiClockReceiver.poll([&](const SwingMetro::MidiRealtimeEvent& event) {
        transportController.handleExternal(event, micros());
    });
    const auto processStartedAtUs = micros();
    transportController.process(processStartedAtUs, internalTicks.ticks());
    transportController.recordProcessDuration(processStartedAtUs, micros());

    const auto matrixNowMs = millis();
    if (matrixScanScheduler.poll(matrixNowMs)) {
        pollMatrixInputs(matrixNowMs);
    }

    const auto encoderNowUs = micros();
    if (encoderSampleScheduler.poll(encoderNowUs)) {
        encoderSampleDiagnostics.recordSample(encoderNowUs);
        pollEncoderInputs(encoderNowUs);
    }

    updateSerialRun();
    syncInternalAlarm();
    SwingMetro::RuntimeTimingSnapshot runtimeSnapshot;
    if (runtimeDiagnosticsExportPending && !transportController.isRunning() &&
        runtimeTimingDiagnostics.readSnapshot(runtimeDiagnosticsRequestGeneration,
                                              runtimeSnapshot)) {
        exportInternalTimingDiagnostics(runtimeSnapshot, runtimeEncoderSnapshot);
        runtimeDiagnosticsExportPending = false;
    }
    if (serialRunCompletionPending && !internalAlarmActive && !runtimeDiagnosticsExportPending) {
        Serial.println(F("swing_metro_control_v1,run_complete"));
        serialRunCompletionPending = false;
    }

    UiSettings settings{tempoCounter.getValue(), swingCounter.getValue(), volumeCounter.getValue(),
                        mainSequencer.getDisplayStepIndex().value_or(UINT8_MAX),
                        mainSequencer.getStepsEnabled()};
    settings.externalClockStatus = transportController.externalStatus();
    settings.externalTempo = transportController.externalBpm();
    uiViewModel.publish(appInputCoordinator.decorateUiSettings(settings));
    appInputCoordinator.processProgramStorage();
    if (!appInputCoordinator.isProgramStorageModalOpen()) {
        (void)programStorageController.syncCurrentProgramIfChanged();
    }
}

/*
 * Second core code
 *
 */

LvglUi uiProvider{runtimeTimingDiagnostics};

void setup1() { uiProvider.setup(); }

void loop1() {
    uiProvider.loop();
    uiProvider.readViewModel(uiViewModel);
}
