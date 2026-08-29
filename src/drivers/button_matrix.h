#pragma once

#include "button_state.h"
#include <Arduino.h>
#include <array>

template <int INPUT_PINS, int OUTPUT_PINS>
class ButtonMatrix {
  public:
    using InputPins = std::array<uint8_t, INPUT_PINS>;
    using OutputPins = std::array<uint8_t, OUTPUT_PINS>;
    using ButtonStates = std::array<std::array<bool, OUTPUT_PINS>, INPUT_PINS>;

    ButtonMatrix(const InputPins& inputPins, const OutputPins& outputPins,
                 uint8_t debouncing = 3) noexcept;
    void init();
    void update();

    void readButtons();
    void getButtonStates(ButtonStates& states) const;
    [[nodiscard]] bool isButtonPressed(int input, int output) const;
    [[nodiscard]] bool isButtonPressed(int number) const;

    [[nodiscard]] constexpr size_t getInputCount() const { return INPUT_PINS; }
    [[nodiscard]] constexpr size_t getOutputCount() const { return OUTPUT_PINS; }

  private:
    static const constexpr int TOTAL_BUTTONS = INPUT_PINS * OUTPUT_PINS;

    inline ButtonState& getButton(int input, int output);
    [[nodiscard]] inline const ButtonState& getButton(int input, int output) const;

    const InputPins _inputPins;
    const OutputPins _outputPins;

    const uint8_t _debouncing;

    std::array<std::array<ButtonState, OUTPUT_PINS>, INPUT_PINS> _buttonStates;
};
