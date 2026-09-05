#include "encoder.h"

/*

Вправо
1: [Volume encoder] State changed: 1, 0 -
2: [Volume encoder] State changed: 0, 0
3: [Volume encoder] State changed: 0, 1
4: [Volume encoder] State changed: 1, 1

Влево
1: [Volume encoder] State changed: 0, 1 -
2: [Volume encoder] State changed: 0, 0
3: [Volume encoder] State changed: 1, 0
4: [Volume encoder] State changed: 1, 1
*/
constexpr const EncoderState ENCODER_STATE_IDLE = {true, true};

constexpr const EncoderState ENCODER_STATE_RIGHT_STEP_1 = { true, false };
constexpr const EncoderState ENCODER_STATE_RIGHT_STEP_2 = { false, false };
constexpr const EncoderState ENCODER_STATE_RIGHT_STEP_3 = { false, true };
constexpr const EncoderState ENCODER_STATE_RIGHT_STEP_4 = ENCODER_STATE_IDLE;

constexpr const EncoderState ENCODER_STATE_LEFT_STEP_1 = { false, true };
constexpr const EncoderState ENCODER_STATE_LEFT_STEP_2 = { false, false };
constexpr const EncoderState ENCODER_STATE_LEFT_STEP_3 = { true, false };
constexpr const EncoderState ENCODER_STATE_LEFT_STEP_4 = ENCODER_STATE_IDLE;

EncoderDirection getDirectionFromStates(const EncoderState& previous, const EncoderState& current) {
    if (previous == ENCODER_STATE_IDLE) {
        if (current == ENCODER_STATE_RIGHT_STEP_1) {
            return EncoderDirection::Right;
        }
        if (current == ENCODER_STATE_LEFT_STEP_1) {
            return EncoderDirection::Left;
        }
    }

    if (previous == ENCODER_STATE_RIGHT_STEP_1) {
        if (current == ENCODER_STATE_RIGHT_STEP_2) {
            return EncoderDirection::Right;
        }
    }

    if (previous == ENCODER_STATE_RIGHT_STEP_2) {
        if (current == ENCODER_STATE_RIGHT_STEP_3) {
            return EncoderDirection::Right;
        }
    }

    if (previous == ENCODER_STATE_RIGHT_STEP_3) {
        if (current == ENCODER_STATE_RIGHT_STEP_4) {
            return EncoderDirection::Right;
        }
    }

    if (previous == ENCODER_STATE_LEFT_STEP_1) {
        if (current == ENCODER_STATE_LEFT_STEP_2) {
            return EncoderDirection::Left;
        }
    }

    if (previous == ENCODER_STATE_LEFT_STEP_2) {
        if (current == ENCODER_STATE_LEFT_STEP_3) {
            return EncoderDirection::Left;
        }
    }

    if (previous == ENCODER_STATE_LEFT_STEP_3) {
        if (current == ENCODER_STATE_LEFT_STEP_4) {
            return EncoderDirection::Left;
        }
    }

    return EncoderDirection::Undefined;
}


Encoder::Encoder(const EncoderSettings& settings) noexcept
    : _pinA(settings.pinA), _pinB(settings.pinB), _pinSwitch(settings.pinSwitch), _handler(settings.handler), _switchHandler(settings.switchHandler)
{}

void Encoder::init() {
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    if (_pinSwitch.has_value()) {
        pinMode(_pinSwitch.value(), INPUT_PULLUP);
    }

    _currentState = getState();
    _previousState = _currentState;
}

void Encoder::update() {
    auto state = getState();

    if (state == _currentState) {
        return;
    }

    _previousState = _currentState;
    _currentState = state;

    auto direction = getDirectionFromStates(_previousState, _currentState);
    if (direction == EncoderDirection::Undefined) {
        return;
    }

    if (_lastDirection == EncoderDirection::Undefined) {
        _lastDirection = direction;
        _accumulatedSteps = 1;
        return;
    }

    if (direction == _lastDirection) {
        _accumulatedSteps += 1;
    } else {
        _lastDirection = direction;
        _accumulatedSteps = 1;
    }

    if (_accumulatedSteps >= _targetSteps) {
        if (_handler != nullptr) {
            _handler(direction);
        }
        _accumulatedSteps = 0;
    }
}

bool Encoder::getPinA() const { 
    return digitalRead(_pinA) > 0;
}

bool Encoder::getPinB() const {
    return digitalRead(_pinB) > 0;
}

EncoderState Encoder::getState() const {
    return EncoderState{
        .a = getPinA(),
        .b = getPinB(),
    };
}

bool Encoder::getSwitch() const {
    if (!_pinSwitch.has_value()) {
        return false;
    }

    return digitalRead(_pinSwitch.value()) > 0;
}