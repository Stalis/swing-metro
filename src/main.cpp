#include <adapters/button_input.h>
#include <adapters/encoder_input.h>
#include <adapters/trigger_input.h>
#include <button_matrix.h>
#include <context_input.h>
#include <encoder.h>
// #include "drivers/spi_display.h"
// #include "drivers/arduino_gfx.h"
#include "components/main_display.h"
#include "components/ui_view_model.h"
#include "drivers/littlefs_program_storage.h"
#include "drivers/lvgl_ui.h"
#include "drivers/usb_midi_adapter.h"
#include "engine/midi_clock_transmitter.h"
#include "engine/midi_step_boundary.h"
#include "input/app_event_handler.h"
#include "input/app_input.h"
#include "input/app_input_coordinator.h"
#include "input/pad_button_ids.h"
#include "input/step_button_inputs.h"
#include "program/program_slot_store.h"
#include "program/program_storage_controller.h"
#include <Arduino.h>
#include <array>
#include <cstddef>
#include <tuple>
#include <variant>

#include "engine/sequencer.h"
#include <Adafruit_TinyUSB.h>
#include <utils/counter.h>

Adafruit_USBD_MIDI usbMidi;
SwingMetro::UsbMidiRealTimeSink midiClockSink{usbMidi};
SwingMetro::UsbMidiRealtimeReceiver midiClockReceiver{usbMidi};

Sequencer mainSequencer;
SwingMetro::MidiClockSettings midiClockSettings;
SwingMetro::MidiClockTransmitter midiClockTransmitter;
SwingMetro::ExternalMidiClock externalMidiClock;
SwingMetro::MidiClockMode previousMidiClockMode = SwingMetro::MidiClockMode::Off;
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
constexpr EncoderSettings tempoEncoderSettings{
    .pinA = 16,
    .pinB = 17,
    .pinSwitch = 18,
    .handler = tempoEncoderHandler,
    .switchHandler = tempoEncoderSwitchHandler,
    .switchReleaseHandler = tempoEncoderSwitchReleaseHandler,
};
Encoder tempoEncoder(tempoEncoderSettings);
Counter<uint8_t> tempoCounter({.step = 1,
                               .value = 120,
                               .minValue = 40,
                               .maxValue = 240,
                               .overflowBehavior = CounterOverflowBehavior::Clamp});

void swingEncoderHandler(EncoderDirection direction);
constexpr EncoderSettings swingEncoderSettings{
    .pinA = 19,
    .pinB = 20,
    .pinSwitch = 21,
    .handler = swingEncoderHandler,
};
Encoder swingEncoder(swingEncoderSettings);
Counter<uint8_t> swingCounter({.step = 1,
                               .value = 50,
                               .minValue = 50,
                               .maxValue = 100,
                               .overflowBehavior = CounterOverflowBehavior::Clamp});

void volumeEncoderHandler(EncoderDirection direction);
void volumeEncoderSwitchHandler();
void volumeEncoderSwitchReleaseHandler();
constexpr EncoderSettings volumeEncoderSettings{
    .pinA = 22,
    .pinB = 26,
    .pinSwitch = 27,
    .handler = volumeEncoderHandler,
    .switchHandler = volumeEncoderSwitchHandler,
    .switchReleaseHandler = volumeEncoderSwitchReleaseHandler,
};
Encoder volumeEncoder(volumeEncoderSettings);
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
    appEventHandler, mainSequencer, midiClockSettings, &programStorageController};
ContextInput::ButtonInputAdapter<SwingMetro::InputId> tempoSwitchInput{{
    SwingMetro::InputId::TempoSwitch,
    500,
}};
ContextInput::ButtonInputAdapter<SwingMetro::InputId> volumeSwitchInput{{
    SwingMetro::InputId::VolumeEncoder,
    500,
}};

constexpr const auto updatables = std::tie(tempoEncoder, swingEncoder, volumeEncoder);
UiViewModel uiViewModel;
bool noteSent = false;
uint8_t lastNoteSent = 0;

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

constexpr uint8_t MIDI_CHANNEL_1 = 0;

void midiSendNoteOn(uint8_t note, uint8_t velocity) {
    const uint8_t packet[4] = {
        0x09, // MIDI command: Note on
        static_cast<uint8_t>(MIDI_CHANNEL_1 | 0x90),
        note,
        velocity,
    };
    usbMidi.writePacket(packet);
}

void midiSendNoteOff(uint8_t note) {
    const uint8_t packet[4] = {
        0x08, // MIDI command: Note off
        static_cast<uint8_t>(MIDI_CHANNEL_1 | 0x80),
        note,
        0,
    };
    usbMidi.writePacket(packet);
}

void handleProgramStorageEvent(const SwingMetro::AppEvent& event) {
    if (std::holds_alternative<SwingMetro::OpenProgramStorage>(event) &&
        appInputCoordinator.isProgramStorageModalOpen()) {
        if (noteSent) {
            midiSendNoteOff(lastNoteSent);
            noteSent = false;
        }
        mainSequencer.stop();
        externalMidiClock.reset();
    } else if (std::holds_alternative<SwingMetro::CloseProgramStorage>(event)) {
        mainSequencer.stop();
        externalMidiClock.reset();
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
    // display_setup();
}

void loop() {
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

    std::apply([](auto&... objects) { (objects.update(), ...); }, updatables);
    handleButtonBatch(tempoSwitchInput.update(millis()));
    handleButtonBatch(volumeSwitchInput.update(millis()));

    const auto nowUs = micros();
    const auto midiClockMode = midiClockSettings.mode();
    if (midiClockMode != previousMidiClockMode) {
        externalMidiClock.reset();
        previousMidiClockMode = midiClockMode;
    }
    if (appInputCoordinator.isProgramStorageModalOpen()) {
        midiClockReceiver.poll([](const SwingMetro::MidiRealtimeEvent&) {});
        externalMidiClock.reset();
        (void)midiClockTransmitter.transition(nowUs, mainSequencer.getBpm(), midiClockMode, false,
                                              midiClockSink);
    } else if (midiClockMode == SwingMetro::MidiClockMode::External) {
        const auto processExternal = [&](const SwingMetro::ExternalMidiClockResult& result) {
            SwingMetro::processExternalMidiClock(mainSequencer, result, noteSent, lastNoteSent,
                                                 midiSendNoteOff, midiSendNoteOn);
        };
        midiClockReceiver.poll([&](const SwingMetro::MidiRealtimeEvent& event) {
            processExternal(externalMidiClock.handle(event));
        });
        processExternal(externalMidiClock.update(nowUs));
        (void)midiClockTransmitter.transition(nowUs, mainSequencer.getBpm(), midiClockMode,
                                              mainSequencer.isRunning(), midiClockSink);
    } else {
        (void)SwingMetro::processMidiStepBoundary(mainSequencer, nowUs, midiClockMode,
                                                  midiClockTransmitter, midiClockSink, noteSent,
                                                  lastNoteSent, midiSendNoteOff, midiSendNoteOn);
    }

    UiSettings settings{tempoCounter.getValue(), swingCounter.getValue(), volumeCounter.getValue(),
                        mainSequencer.getDisplayStepIndex().value_or(UINT8_MAX),
                        mainSequencer.getStepsEnabled()};
    settings.externalClockStatus = externalMidiClock.status();
    settings.externalTempo = externalMidiClock.bpm();
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

// MainDisplay mainDisplay(*gfx);
LVGL_Ui ui_provider{};

void setup1() {
    ui_provider.setup();

    // mainDisplay.init();
}

void loop1() {
    ui_provider.loop();
    ui_provider.readViewModel(uiViewModel);
    //   const UiSettings settings = uiViewModel.read();
    //   mainDisplay.updateTempo(settings.tempo);
    //   mainDisplay.updateSwing(settings.swing);
    //   mainDisplay.updateVolume(settings.volume);

    //   mainDisplay.updateNotesStates(settings.notesState, settings.activeNote);

    //   gfx->flush();
}
