#pragma once

#include <cstdint>
#include <optional>

enum class EncoderDirection : std::uint8_t {
    Clockwise = 0,
    CounterClockwise = 1,
    Left = CounterClockwise,
    Right = Clockwise,
    Undefined = 0xFF,
};

using EncoderHandler = void (*)(EncoderDirection direction);
using SwitchHandler = void (*)();

struct EncoderSettings {
    std::uint8_t pinA;
    std::uint8_t pinB;
    std::optional<std::uint8_t> pinSwitch = std::nullopt;

    EncoderHandler handler = nullptr;
    SwitchHandler switchHandler = nullptr;
    std::uint8_t switchDebouncing = 3;
    SwitchHandler switchReleaseHandler = nullptr;
};

struct EncoderState {
    bool a : 1;
    bool b : 1;

    bool operator==(const EncoderState& right) const { return a == right.a && b == right.b; }

    bool operator!=(const EncoderState& right) const { return !operator==(right); }
};

class Encoder {
  public:
    Encoder(const EncoderSettings& settings) noexcept;

    void init();
    // Call regularly from one context; switchHandler runs there on active-low press.
    void update();

    [[nodiscard]] bool getPinA() const;
    [[nodiscard]] bool getPinB() const;
    [[nodiscard]] EncoderState getState() const;
    [[nodiscard]] bool getSwitch() const;

  private:
    std::uint8_t _pinA;
    std::uint8_t _pinB;
    std::optional<std::uint8_t> _pinSwitch;

    bool _aBuf = false;
    bool _bBuf = false;

    static const std::uint8_t _targetSteps = 4;
    std::uint8_t _accumulatedSteps = 0;
    EncoderDirection _lastDirection = EncoderDirection::Undefined;
    EncoderState _currentState;
    EncoderState _previousState;

    EncoderHandler _handler;
    SwitchHandler _switchHandler;
    SwitchHandler _switchReleaseHandler;
    std::uint8_t _switchDebouncing;
    std::uint8_t _currentSwitchDebouncing = 0;
    bool _switchCandidateState = false;
    bool _previousSwitchState = false;
    bool _currentSwitchState = false;

    void updateSwitch();
};
