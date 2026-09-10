#pragma once

#include "button_state.h"
#include <Arduino.h>
#include <array>

template <int INPUT_PINS, int OUTPUT_PINS>
struct DefaultButtonIds {
    static constexpr std::array<uint8_t, INPUT_PINS * OUTPUT_PINS> values = [] {
        std::array<uint8_t, INPUT_PINS * OUTPUT_PINS> ids{};
        for (uint8_t index = 0; index < ids.size(); ++index) {
            ids[index] = index;
        }
        return ids;
    }();
};

template <int INPUT_PINS, int OUTPUT_PINS,
          typename ButtonIds = DefaultButtonIds<INPUT_PINS, OUTPUT_PINS>>
class ButtonMatrix {
  public:
    using InputPins = std::array<uint8_t, INPUT_PINS>;
    using OutputPins = std::array<uint8_t, OUTPUT_PINS>;
    using ButtonStates = std::array<std::array<bool, OUTPUT_PINS>, INPUT_PINS>;

    ButtonMatrix(const InputPins& inputPins, const OutputPins& outputPins,
                 uint8_t debouncing = 3) noexcept;
    void init();
    void update();

    // Call readButtons(), consume events, then update() from the same context.
    void readButtons();
    void getButtonStates(ButtonStates& states) const;
    [[nodiscard]] bool isButtonPressed(int input, int output) const;
    [[nodiscard]] bool isButtonPressed(int number) const;
    // ButtonIds::values maps physical indices to application IDs.
    [[nodiscard]] constexpr uint8_t getButtonId(int input, int output) const {
        return ButtonIds::values[input * OUTPUT_PINS + output];
    }
    [[nodiscard]] constexpr uint8_t getButtonId(int number) const {
        return ButtonIds::values[number];
    }

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

#include "button_matrix.ipp"
