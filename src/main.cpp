#include "components/ui_view_model.h"
#include "drivers/littlefs_program_storage.h"
#include "drivers/lvgl_ui.h"
#include "drivers/pico_internal_tick_alarm.h"
#include "drivers/usb_midi_adapter.h"
#include "engine/transport_controller.h"
#include "input/app_event_handler.h"
#include "input/app_input.h"
#include "input/app_input_coordinator.h"
#include "input/pad_button_ids.h"
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
SwingMetro::UsbMidiPacketSink midiSink{usbMidi};
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
ButtonMatrix<4, 4, SwingMetro::PadButtonIds> buttonMatrix(INPUT_PINS, OUTPUT_PINS, 3);
SwingMetro::StepButtonInputs stepButtonInputs;

void tempoEncoderHandler(EncoderDirection direction);
void tempoEncoderSwitchHandler();
void tempoEncoderSwitchReleaseHandler();
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
                               .maxValue = 100,
                               .overflowBehavior = CounterOverflowBehavior::Clamp});

void volumeEncoderHandler(EncoderDirection direction);
void volumeEncoderSwitchHandler();
void volumeEncoderSwitchReleaseHandler();
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
    500,
}};
ContextInput::ButtonInputAdapter<SwingMetro::InputId> volumeSwitchInput{{
    SwingMetro::InputId::VolumeEncoder,
    500,
}};

constexpr const auto UPDATABLES = std::tie(tempoEncoder, swingEncoder, volumeEncoder);
UiViewModel uiViewModel;
bool internalAlarmActive = false;
uint8_t internalAlarmBpm = 0;

template <typename TAdapter>
void handleEncoderDirection(const TAdapter& adapter, EncoderDirection direction) {
    const auto input = adapter.translate(direction);
    if (!input.has_value()) {
        return;
    }

    appInputCoordinator.dispatch(*input, micros());
}

void handleProgramStorageEvent(const SwingMetro::AppEvent& event);

void handleButtonBatch(const SwingMetro::StepButtonInputs::Batch& batch) {
    for (std::size_t index = 0; index < batch.size(); ++index) {
        const auto& input = batch[index];
        if (const auto event = appInputCoordinator.dispatch(input, micros()); event.has_value()) {
            handleProgramStorageEvent(*event);
        }
    }
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

void tempoEncoderSwitchHandler() { handleButtonBatch(tempoSwitchInput.onPressed(millis())); }

void tempoEncoderSwitchReleaseHandler() {
    handleButtonBatch(tempoSwitchInput.onReleased(millis()));
}

void volumeEncoderSwitchHandler() { handleButtonBatch(volumeSwitchInput.onPressed(millis())); }

void volumeEncoderSwitchReleaseHandler() {
    handleButtonBatch(volumeSwitchInput.onReleased(millis()));
}

void handleProgramStorageEvent(const SwingMetro::AppEvent& event) {
    if (std::holds_alternative<SwingMetro::OpenProgramStorage>(event) &&
        appInputCoordinator.isProgramStorageModalOpen()) {
        transportController.openStorage();
    } else if (std::holds_alternative<SwingMetro::CloseProgramStorage>(event)) {
        transportController.closeStorage();
    }
}

void syncInternalAlarm() {
    const bool shouldRun = transportController.usesInternalTiming();
    if (shouldRun && !internalAlarmActive) {
        internalTickAlarm.start(mainSequencer.getBpm());
        internalAlarmActive = true;
        internalAlarmBpm = mainSequencer.getBpm();
    } else if (!shouldRun && internalAlarmActive) {
        internalTickAlarm.stop();
        internalAlarmActive = false;
    } else if (shouldRun && internalAlarmBpm != mainSequencer.getBpm()) {
        internalTickAlarm.setBpm(mainSequencer.getBpm());
        internalAlarmBpm = mainSequencer.getBpm();
    }
}

void setup() {

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
}

void loop() {
    const auto nowUs = micros();
    midiClockReceiver.poll([&](const SwingMetro::MidiRealtimeEvent& event) {
        transportController.handleExternal(event);
    });
    transportController.process(nowUs, internalTicks.ticks());

    buttonMatrix.readButtons();
    const auto now = millis();

    for (int physicalIndex = 0; physicalIndex < STEPS_COUNT; ++physicalIndex) {
        const auto step = buttonMatrix.getButtonId(physicalIndex);
        if (buttonMatrix.isButtonJustPressed(physicalIndex)) {
            handleButtonBatch(stepButtonInputs.onPressed(step, now));
        } else if (buttonMatrix.isButtonJustReleased(physicalIndex)) {
            handleButtonBatch(stepButtonInputs.onReleased(step, now));
        } else if (buttonMatrix.isButtonHolding(physicalIndex)) {
            handleButtonBatch(stepButtonInputs.update(step, now));
        }
    }

    buttonMatrix.update();

    std::apply([](auto&... objects) { (objects.update(), ...); }, UPDATABLES);
    handleButtonBatch(tempoSwitchInput.update(millis()));
    handleButtonBatch(volumeSwitchInput.update(millis()));

    syncInternalAlarm();

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

LvglUi uiProvider{};

void setup1() { uiProvider.setup(); }

void loop1() {
    uiProvider.loop();
    uiProvider.readViewModel(uiViewModel);
}
