#pragma once
#include "button_matrix.h"

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::ButtonMatrix(const InputPins& inputPins,
                                                                const OutputPins& outputPins,
                                                                uint8_t debouncing) noexcept
    : _inputPins(inputPins), _outputPins(outputPins), _debouncing(debouncing), _buttonStates() {}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
void ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::init() {
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

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
void ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::readButtons() {
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

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
void ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::update() {
    for (uint8_t output = 0; output < OUTPUT_PINS; ++output) {
        for (uint8_t input = 0; input < INPUT_PINS; ++input) {
            getButton(input, output).update();
        }
    }
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
void ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::getButtonStates(
    ButtonStates& states) const {
    for (int input = 0; input < INPUT_PINS; ++input) {
        for (int output = 0; output < OUTPUT_PINS; ++output) {
            states[input][output] = isButtonPressed(input, output);
        }
    }
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonJustPressed(int input,
                                                                            int output) const {
    return getButton(input, output).isJustPressed();
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonJustPressed(int number) const {
    return isButtonJustPressed(number / OUTPUT_PINS, number % OUTPUT_PINS);
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonHolding(int input,
                                                                        int output) const {
    return getButton(input, output).isHolding();
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonHolding(int number) const {
    return isButtonHolding(number / OUTPUT_PINS, number % OUTPUT_PINS);
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonJustReleased(int input,
                                                                             int output) const {
    return getButton(input, output).isJustReleased();
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonJustReleased(int number) const {
    return isButtonJustReleased(number / OUTPUT_PINS, number % OUTPUT_PINS);
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonReleased(int input,
                                                                         int output) const {
    return getButton(input, output).isReleased();
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonReleased(int number) const {
    return isButtonReleased(number / OUTPUT_PINS, number % OUTPUT_PINS);
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonPressed(int input,
                                                                        int output) const {
    return isButtonJustPressed(input, output);
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
bool ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::isButtonPressed(int number) const {
    return isButtonJustPressed(number);
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
inline const ButtonState&
ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::getButton(int input, int output) const {
    return _buttonStates[input][output];
}

template <int INPUT_PINS, int OUTPUT_PINS, typename TButtonIds>
inline ButtonState& ButtonMatrix<INPUT_PINS, OUTPUT_PINS, TButtonIds>::getButton(int input,
                                                                                 int output) {
    return _buttonStates[input][output];
}
