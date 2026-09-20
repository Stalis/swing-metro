#pragma once

#include <time_debouncer.h>

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
using SwitchHandler = void (*)(std::uint32_t nowUs);

constexpr std::uint32_t ENCODER_SWITCH_DEBOUNCE_DURATION_US = 3'000;

struct EncoderSettings {
    std::uint8_t pinA;
    std::uint8_t pinB;
    std::optional<std::uint8_t> pinSwitch = std::nullopt;

    EncoderHandler handler = nullptr;
    SwitchHandler switchHandler = nullptr;
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
    void update(std::uint32_t nowUs);

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

    static const std::uint8_t _TARGET_STEPS = 4;
    std::uint8_t _accumulatedSteps = 0;
    EncoderDirection _lastDirection = EncoderDirection::Undefined;
    EncoderState _currentState;
    EncoderState _previousState;

    EncoderHandler _handler;
    SwitchHandler _switchHandler;
    SwitchHandler _switchReleaseHandler;
    InputTiming::TimeDebouncer _switchDebouncer;

    void updateSwitch(std::uint32_t nowUs);
};
