#include <button_matrix.h>
#include <encoder.h>
// #include "drivers/spi_display.h"
// #include "drivers/arduino_gfx.h"
#include "components/main_display.h"
#include "components/ui_view_model.h"
#include "drivers/lvgl_ui.h"
#include <Arduino.h>
#include <array>
#include <tuple>

#include "engine/sequencer.h"
#include <Adafruit_TinyUSB.h>
#include <utils/counter.h>

constexpr int SERIAL_BAUD_RATE = 115200;

Adafruit_USBD_MIDI usbMidi;

Sequencer mainSequencer;

struct PadButtonIds {
    static constexpr std::array<uint8_t, 16> values = {
        0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15,
    };
};

constexpr std::array<uint8_t, 4> INPUT_PINS = {D0, D1, D2, D3};
constexpr std::array<uint8_t, 4> OUTPUT_PINS = {D4, D5, D6, D7};
ButtonMatrix<4, 4, PadButtonIds> buttonMatrix(INPUT_PINS, OUTPUT_PINS, 3);

void tempoEncoderHandler(EncoderDirection direction);
void tempoEncoderSwitchHandler();
constexpr EncoderSettings tempoEncoderSettings{
    .pinA = 16,
    .pinB = 17,
    .pinSwitch = 18,
    .handler = tempoEncoderHandler,
    .switchHandler = tempoEncoderSwitchHandler,
};
Encoder tempoEncoder(tempoEncoderSettings);
Counter<uint8_t> tempoCounter({.step = 1,
                               .value = 120,
                               .minValue = 40,
                               .maxValue = 240,
                               .overflowBehavior = CounterOverflowBehavior::Clamp});

void swingEncoderHandler(EncoderDirection direction);
void swingEncoderSwitchHandler();
constexpr EncoderSettings swingEncoderSettings{
    .pinA = 19,
    .pinB = 20,
    .pinSwitch = 21,
    .handler = swingEncoderHandler,
    .switchHandler = swingEncoderSwitchHandler,
};
Encoder swingEncoder(swingEncoderSettings);
Counter<uint8_t> swingCounter({.step = 1,
                               .value = 50,
                               .minValue = 50,
                               .maxValue = 100,
                               .overflowBehavior = CounterOverflowBehavior::Clamp});

void volumeEncoderHandler(EncoderDirection direction);
void volumeEncoderSwitchHandler();
constexpr EncoderSettings volumeEncoderSettings{
    .pinA = 22,
    .pinB = 26,
    .pinSwitch = 27,
    .handler = volumeEncoderHandler,
    .switchHandler = volumeEncoderSwitchHandler,
};
Encoder volumeEncoder(volumeEncoderSettings);
Counter<uint8_t> volumeCounter({.step = 1,
                                .value = 100,
                                .minValue = 0,
                                .maxValue = 100,
                                .overflowBehavior = CounterOverflowBehavior::Clamp});

constexpr const auto updatables = std::tie(tempoEncoder, swingEncoder, volumeEncoder);
UiViewModel uiViewModel;

void volumeEncoderHandler(EncoderDirection direction) {
    if (direction == EncoderDirection::Right) {
        volumeCounter.stepUp();
    } else if (direction == EncoderDirection::Left) {
        volumeCounter.stepDown();
    }
}

void swingEncoderHandler(EncoderDirection direction) {
    if (direction == EncoderDirection::Right) {
        swingCounter.stepUp();
    } else if (direction == EncoderDirection::Left) {
        swingCounter.stepDown();
    }
}

void tempoEncoderHandler(EncoderDirection direction) {
    if (direction == EncoderDirection::Right) {
        tempoCounter.stepUp();
    } else if (direction == EncoderDirection::Left) {
        tempoCounter.stepDown();
    }
    mainSequencer.setBpm(tempoCounter.getValue());
}

void tempoEncoderSwitchHandler() { Serial.println("[Tempo encoder] Pressed"); }

void swingEncoderSwitchHandler() { Serial.println("[Swing encoder] Pressed"); }

void volumeEncoderSwitchHandler() { Serial.println("[Volume encoder] Pressed"); }

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
        0x08, // MIDI command: Note on
        static_cast<uint8_t>(MIDI_CHANNEL_1 | 0x80),
        note,
        0,
    };
    usbMidi.writePacket(packet);
}

void setup() {

    Serial.begin(SERIAL_BAUD_RATE);

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
    uiViewModel.publish(
        {tempoCounter.getValue(), swingCounter.getValue(), volumeCounter.getValue()});

    mainSequencer.sync(micros());
    // display_setup();
}

uint8_t volume_last_value = volumeCounter.getValue();
uint8_t swing_last_value = swingCounter.getValue();
uint8_t tempo_last_value = tempoCounter.getValue();

bool note_sent = false;
uint8_t last_note_sent = 0;

void loop() {
    buttonMatrix.readButtons();

    for (size_t input = 0; input < buttonMatrix.getInputCount(); input++) {
        for (size_t output = 0; output < buttonMatrix.getOutputCount(); output++) {
            if (buttonMatrix.isButtonPressed(input, output)) {
                auto buttonId = buttonMatrix.getButtonId(input, output);

                mainSequencer.toggleStep(buttonId);

                //   Serial.print("Pressed button #");
                //   Serial.print(buttonId);
                //   Serial.print("\t [");
                //   Serial.print(input);
                //   Serial.print("; ");
                //   Serial.print(output);
                //   Serial.println(" ]");
            }
        }
    }

    buttonMatrix.update();

    std::apply([](auto&... objects) { (objects.update(), ...); }, updatables);

    if (volumeCounter.getValue() != volume_last_value) {
        Serial.print("[Volume encoder] Value changed: ");
        Serial.println(volumeCounter.getValue());
        volume_last_value = volumeCounter.getValue();
    }

    if (swingCounter.getValue() != swing_last_value) {
        Serial.print("[Swing encoder] Value changed: ");
        Serial.println(swingCounter.getValue());
        swing_last_value = swingCounter.getValue();
    }

    if (tempoCounter.getValue() != tempo_last_value) {
        Serial.print("[Tempo encoder] Value changed: ");
        Serial.println(tempoCounter.getValue());
        tempo_last_value = tempoCounter.getValue();
    }

    if (mainSequencer.update(micros())) {
        midiSendNoteOff(last_note_sent);
        Serial.printf("step=%u enabled=%u t=%lu\n", mainSequencer.getCurrentStepIndex(),
                      static_cast<int>(mainSequencer.isCurrentStepEnabled()), micros());

        if (mainSequencer.isCurrentStepEnabled()) {
            auto note = mainSequencer.currentStepMidiNote();

            last_note_sent = note;
            note_sent = true;
            midiSendNoteOn(note, mainSequencer.currentStepVelocity());
        }
    }

    uiViewModel.publish({
        tempoCounter.getValue(),
        swingCounter.getValue(),
        volumeCounter.getValue(),
        mainSequencer.getCurrentStepIndex(),
        mainSequencer.getStepsEnabled(),
    });
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
