#pragma once

#include <Arduino.h>
#include <optional>
#include <utils/round_buffer.h>

enum class EncoderDirection : uint8_t {
    Clockwise = 0,
    CounterClockwise = 1,
    Left = CounterClockwise,
    Right = Clockwise,
    Undefined = 0xFF
};

using EncoderHandler = void(*)(EncoderDirection direction);
using SwitchHandler = void(*)();

struct EncoderSettings {
    uint8_t pinA;
    uint8_t pinB;
    std::optional<uint8_t> pinSwitch = std::nullopt;

    EncoderHandler handler = nullptr;
    SwitchHandler switchHandler = nullptr;
};

struct EncoderState {
    bool a : 1;
    bool b : 1;

    bool operator==(const EncoderState& right) const {
        return a == right.a && b == right.b;
    }

    bool operator!=(const EncoderState& right) const {
        return !operator==(right);
    }
};

class Encoder {
public:
    Encoder(const EncoderSettings& settings) noexcept;

    void init();
    void update();

    [[nodiscard]] bool getPinA() const;
    [[nodiscard]] bool getPinB() const;
    [[nodiscard]] EncoderState getState() const;
    [[nodiscard]] bool getSwitch() const;

private:
    uint8_t _pinA;
    uint8_t _pinB;
    std::optional<uint8_t> _pinSwitch;

    bool _aBuf = false;
    bool _bBuf = false;

    static const uint8_t _targetSteps = 4;
    uint8_t _accumulatedSteps = 0;
    EncoderDirection _lastDirection = EncoderDirection::Undefined;
    EncoderState _currentState;
    EncoderState _previousState;

    EncoderHandler _handler;
    SwitchHandler _switchHandler;

    RoundBuffer<EncoderState> _stateBuffer{4};
};