#pragma once

#include <cstdint>

enum class CounterOverflowBehavior : uint8_t {
    Clamp,
    WrapAround
};

enum class CounterDirection : uint8_t {
    Up,
    Down
};

template <typename T>
struct CounterSettings {
    T step;
    T value;
    T minValue;
    T maxValue;
    CounterOverflowBehavior overflowBehavior = CounterOverflowBehavior::Clamp;
};

template <typename T>
class Counter {
public:
    [[nodiscard]] Counter(const CounterSettings<T>& settings) : _step(settings.step), _value(settings.value), _minValue(settings.minValue), _maxValue(settings.maxValue), _overflowBehavior(settings.overflowBehavior) {}

    void stepUp() {
        step(CounterDirection::Up);
    }

    void stepDown() {
        step(CounterDirection::Down);
    }

    void step(CounterDirection direction) {
        auto newValue = direction == CounterDirection::Up ? _value + _step : _value - _step;
        if (_overflowBehavior == CounterOverflowBehavior::Clamp) {
            if (newValue > _maxValue) {
                _value = _maxValue;
            } else if (newValue < _minValue) {
                _value = _minValue;
            } else {
                _value = newValue;
            }
        } else if (_overflowBehavior == CounterOverflowBehavior::WrapAround) {
            if (newValue > _maxValue) {
                _value = _minValue + (newValue - _maxValue - 1);
            } else if (newValue < _minValue) {
                _value = _maxValue - (_minValue - newValue - 1);
            } else {
                _value = newValue;
            }
        }
    }

    [[nodiscard]] auto getValue() const {
        return _value;
    }

    void setStep(const T step) {
        _step = step;
    }

    [[nodiscard]] auto getStep() const {
        return _step;
    }
private:
    T _step;
    T _value;
    T _minValue;
    T _maxValue;
    CounterOverflowBehavior _overflowBehavior;
};