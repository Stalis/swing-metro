#include "components/ui_view_model.h"
#include "drivers/littlefs_program_storage.h"
#include "drivers/lvgl_ui.h"
#include "drivers/pico_internal_tick_alarm.h"
#include "drivers/usb_midi_adapter.h"
#include "engine/runtime_timing_diagnostics.h"
#include "engine/transport_controller.h"
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
#include "engine/fault_midi_message_sink.h"
#endif
#include "drivers/diagnostics/arduino_diagnostic_console.h"
#include "input/app_event_handler.h"
#include "input/app_input.h"
#include "input/app_input_coordinator.h"
#include "input/encoder_sample_diagnostics.h"
#include "input/pad_button_ids.h"
#include "input/periodic_scheduler.h"
#include "input/serial_run_controller.h"
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
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
SwingMetro::UsbMidiMessageSink normalMidiSink{usbMidi};
SwingMetro::FaultMidiMessageSink midiSink{normalMidiSink};
#else
SwingMetro::UsbMidiMessageSink midiSink{usbMidi};
#endif
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
SwingMetro::ArduinoDiagnosticConsole diagnosticConsole;
SwingMetro::DiagnosticsSerializer diagnosticsSerializer{diagnosticConsole};
SwingMetro::DiagnosticsCapture diagnosticsCapture{transportController, internalTicks,
                                                  encoderSampleDiagnostics,
                                                  runtimeTimingDiagnostics, diagnosticsSerializer};
SwingMetro::SerialRunController serialRunController{diagnosticConsole,
                                                    mainSequencer,
                                                    midiClockSettings,
                                                    transportController,
                                                    tempoCounter,
                                                    swingCounter,
                                                    runtimeTimingDiagnostics,
                                                    encoderSampleDiagnostics,
                                                    diagnosticsCapture
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
                                                    ,
                                                    &midiSink
#endif
};

template <typename TAdapter>
void handleEncoderDirection(const TAdapter& adapter, EncoderDirection direction) {
    const auto input = adapter.translate(direction);
    if (!input.has_value()) {
        return;
    }

    appInputCoordinator.dispatch(*input, micros());
}

void handleProgramStorageEvent(const SwingMetro::AppEvent& event, std::uint32_t nowUs);

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
        diagnosticsCapture.beginWindow();
        internalTickAlarm.start(mainSequencer.getBpm(),
                                transportController.internalTickDiscardReason());
        internalAlarmActive = true;
        internalAlarmBpm = mainSequencer.getBpm();
    } else if (!shouldRun && internalAlarmActive) {
        internalTickAlarm.stop(transportController.internalTickDiscardReason());
        internalAlarmActive = false;
        diagnosticsCapture.requestExport();
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
    serialRunController.pollSerialRunCommand();
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

    serialRunController.updateSerialRun();
    syncInternalAlarm();
    diagnosticsCapture.exportIfReady();
    serialRunController.completeIfReady(internalAlarmActive);

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
