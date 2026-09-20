#include "button_state.h"

ButtonState::ButtonState() noexcept : _debouncer(false), _previousState(0), _currentState(0) {}

void ButtonState::reset(uint8_t state) {
    _debouncer.reset(state);
    _previousState = state;
    _currentState = state;
}

void ButtonState::newState(uint8_t state, std::uint32_t nowMs) {
    if (const auto confirmed = _debouncer.observe(state != 0, nowMs, MATRIX_DEBOUNCE_DURATION_MS);
        confirmed.has_value()) {
        _currentState = *confirmed;
    }
}

void ButtonState::update() {
    if (_previousState != _currentState) {
        _previousState = _currentState;
    }
}
