#pragma once

#include <Arduino.h>

class ButtonState {
  public:
    ButtonState() noexcept;
    ButtonState(uint8_t debouncing) noexcept;

    void setDebounce(uint8_t debouncing);
    void newState(uint8_t state);
    void update();

    [[nodiscard]] bool isJustPressed() const { return _previousState == 0 && _currentState == 1; }
    [[nodiscard]] bool isHolding() const { return _previousState && _currentState; }
    [[nodiscard]] bool isJustReleased() const { return _previousState == 1 && _currentState == 0; }
    [[nodiscard]] bool isReleased() const { return !_previousState && !_currentState; }

  private:
    uint8_t _debouncing : 4;
    uint8_t _currentDebouncing : 4;
    uint8_t _candidateState : 1;
    uint8_t _previousState : 1;
    uint8_t _currentState : 1;
};