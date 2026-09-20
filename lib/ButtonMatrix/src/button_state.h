#pragma once

#include <time_debouncer.h>

#include <cstdint>

constexpr std::uint32_t MATRIX_DEBOUNCE_DURATION_MS = 15;

class ButtonState {
  public:
    ButtonState() noexcept;

    void reset(uint8_t state);
    void newState(uint8_t state, std::uint32_t nowMs);
    void update();

    [[nodiscard]] bool isJustPressed() const { return _previousState == 0 && _currentState == 1; }
    [[nodiscard]] bool isHolding() const { return _previousState && _currentState; }
    [[nodiscard]] bool isJustReleased() const { return _previousState == 1 && _currentState == 0; }
    [[nodiscard]] bool isReleased() const { return !_previousState && !_currentState; }

  private:
    InputTiming::TimeDebouncer _debouncer;
    uint8_t _previousState : 1;
    uint8_t _currentState : 1;
};
