#include "drivers/button_matrix.h"
#include "drivers/encoder.h"
#include "drivers/spi_display.h"
#include <Arduino.h>
#include <array>
#include <pico/time.h>
#include <tuple>

#include "utils/counter.h"

// put function declarations here:

constexpr std::array<uint8_t, 4> INPUT_PINS = {D0, D1, D2, D3};
constexpr std::array<uint8_t, 4> OUTPUT_PINS = {D4, D5, D6, D7};
ButtonMatrix<4, 4> buttonMatrix(INPUT_PINS, OUTPUT_PINS, 1);

void tempoEncoderHandler(EncoderDirection direction);
constexpr EncoderSettings tempoEncoderSettings{
    .pinA = 16,
    .pinB = 17,
    .pinSwitch = 18,
    .handler = tempoEncoderHandler,
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
                               .minValue = 0,
                               .maxValue = 100,
                               .overflowBehavior = CounterOverflowBehavior::Clamp});

void volumeEncoderHandler(EncoderDirection direction);
constexpr EncoderSettings volumeEncoderSettings{
    .pinA = 22,
    .pinB = 26,
    .pinSwitch = 27,
    .handler = volumeEncoderHandler,
};
Encoder volumeEncoder(volumeEncoderSettings);
Counter<uint8_t> volumeCounter({.step = 1,
                                .value = 50,
                                .minValue = 0,
                                .maxValue = 100,
                                .overflowBehavior = CounterOverflowBehavior::Clamp});

constexpr const auto updatables = std::tie(tempoEncoder, swingEncoder, volumeEncoder);

repeating_timer_t timer;

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
}

bool button_scan(repeating_timer_t*) {
    buttonMatrix.readButtons();
    return true;
}

void setup() {

    Serial.begin(9600);
    buttonMatrix.init();

    add_repeating_timer_ms(1, button_scan, nullptr, &timer);

    tempoEncoder.init();
    swingEncoder.init();
    volumeEncoder.init();

    // display_setup();
}

void setup1() {
  display_setup();
}

uint8_t volume_last_value = volumeCounter.getValue();
uint8_t swing_last_value = swingCounter.getValue();
uint8_t tempo_last_value = tempoCounter.getValue();


void loop() {
    // for (size_t input = 0; input < buttonMatrix.getInputCount(); input++) {
    //   for (size_t output = 0; output < buttonMatrix.getOutputCount(); output++) {
    //     if (buttonMatrix.isButtonPressed(input, output)) {
    //       Serial.print("Pressed button #");
    //       Serial.print((output*4) + input);
    //       Serial.print("\t [");
    //       Serial.print(input);
    //       Serial.print("; ");
    //       Serial.print(output);
    //       Serial.println(" ]");
    //     }
    //   }
    // }

    // buttonMatrix.update();

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
}

void loop1() {
  display_loop();
}
