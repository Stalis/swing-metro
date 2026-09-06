#pragma once
#include "button_matrix.h"

template <int INPUT_PINS, int OUTPUT_PINS>
ButtonMatrix<INPUT_PINS, OUTPUT_PINS>::ButtonMatrix(const InputPins& inputPins,
                                                     const OutputPins& outputPins,
                                                     uint8_t debouncing) noexcept
    : _inputPins(inputPins), _outputPins(outputPins), _debouncing(debouncing), _buttonStates() {}

template <int INPUT_PINS, int OUTPUT_PINS>
void ButtonMatrix<INPUT_PINS, OUTPUT_PINS>::init() {
    for (int input = 0; input < INPUT_PINS; ++input) {
        for (int output = 0; output < OUTPUT_PINS; ++output) {
            getButton(input, output).setDebounce(_debouncing);
        }
    }

    for (int input = 0; input < INPUT_PINS; ++input) {
        pinMode(_inputPins[input], INPUT_PULLUP);
    }
    for (int output = 0; output < OUTPUT_PINS; ++output) {
        pinMode(_outputPins[output], OUTPUT);
        digitalWrite(_outputPins[output], HIGH);
    }
}

template <int INPUT_PINS, int OUTPUT_PINS>
void ButtonMatrix<INPUT_PINS, OUTPUT_PINS>::readButtons() {
    for (uint8_t output = 0; output < OUTPUT_PINS; ++output) {
        digitalWrite(_outputPins[output], LOW);
        delayMicroseconds(5);

        for (uint8_t input = 0; input < INPUT_PINS; ++input) {
            bool buf = digitalRead(_inputPins[input]) == LOW;
            getButton(input, output).newState(buf);
        }

        digitalWrite(_outputPins[output], HIGH);
    }
}

template <int INPUT_PINS, int OUTPUT_PINS>
void ButtonMatrix<INPUT_PINS, OUTPUT_PINS>::update() {
    for (uint8_t output = 0; output < OUTPUT_PINS; ++output) {
        for (uint8_t input = 0; input < INPUT_PINS; ++input) {
            getButton(input, output).update();
        }
    }
}

template <int INPUT_PINS, int OUTPUT_PINS>
void ButtonMatrix<INPUT_PINS, OUTPUT_PINS>::getButtonStates(ButtonStates& states) const {
    for (int input = 0; input < INPUT_PINS; ++input) {
        for (int output = 0; output < OUTPUT_PINS; ++output) {
            states[input][output] = isButtonPressed(input, output);
        }
    }
}

template <int INPUT_PINS, int OUTPUT_PINS>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS>::isButtonPressed(int input, int output) const {
    return getButton(input, output).isJustPressed();
}

template <int INPUT_PINS, int OUTPUT_PINS>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS>::isButtonPressed(int number) const {
    return isButtonPressed(number / OUTPUT_PINS, number % OUTPUT_PINS);
}

template <int INPUT_PINS, int OUTPUT_PINS>
inline const ButtonState& ButtonMatrix<INPUT_PINS, OUTPUT_PINS>::getButton(int input,
                                                                             int output) const {
    return _buttonStates[input][output];
}

template <int INPUT_PINS, int OUTPUT_PINS>
inline ButtonState& ButtonMatrix<INPUT_PINS, OUTPUT_PINS>::getButton(int input, int output) {
    return _buttonStates[input][output];
}
