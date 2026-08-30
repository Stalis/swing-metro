#include <Arduino.h>
#include <array>
#include <pico/time.h>
#include "drivers/button_matrix.h"
#include "drivers/7seg_disp.h"

// put function declarations here:

constexpr std::array<uint8_t, 4> INPUT_PINS = {D0, D1, D2, D3};
constexpr std::array<uint8_t, 4> OUTPUT_PINS = {D4, D5, D6, D7};
ButtonMatrix<4,4> buttonMatrix(INPUT_PINS, OUTPUT_PINS, 1);

SevenSegmentDisplay sevenSeg(ShiftRegisterPins{
  data: D15,
  clock: D14,
  latch: D13
});

repeating_timer_t timer;

bool button_scan(repeating_timer_t*) {
  buttonMatrix.readButtons();
  return true;
}

void setup() {

  Serial.begin(9600);
  buttonMatrix.init();

  add_repeating_timer_ms(
    1,
    button_scan,
    nullptr,
    &timer
  );
  sevenSeg.init();
  sevenSeg.setNumber(1111);

}

void loop() {
  for (size_t input = 0; input < buttonMatrix.getInputCount(); input++) {
    for (size_t output = 0; output < buttonMatrix.getOutputCount(); output++) {
      if (buttonMatrix.isButtonPressed(input, output)) {
        Serial.print("Pressed button #");
        Serial.print((output*4) + input);
        Serial.print("\t [");
        Serial.print(input);
        Serial.print("; ");
        Serial.print(output);
        Serial.println(" ]");
      }
    }
  }

  buttonMatrix.update();

  sevenSeg.update();
  tone(D27, 1000, 100);
  delay(500);
}
