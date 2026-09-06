#include "button_state.h"

ButtonState::ButtonState() noexcept : ButtonState(3) {}
ButtonState::ButtonState(uint8_t debouncing) noexcept
    : _debouncing(debouncing & 0x0F), _currentDebouncing(0), _candidateState(0), _previousState(0),
      _currentState(0) {};

void ButtonState::setDebounce(uint8_t debouncing) { _debouncing = debouncing & 0x0F; }

void ButtonState::newState(uint8_t state) {
    if (state != _candidateState) {
        _currentDebouncing = 0;
        _candidateState = state;
    }

    if (_currentDebouncing < _debouncing) {
        _currentDebouncing++;
        return;
    }
    _currentDebouncing = 0;

    _currentState = state;
}

void ButtonState::update() {
    if (_previousState != _currentState) {
        _previousState = _currentState;
    }
}
