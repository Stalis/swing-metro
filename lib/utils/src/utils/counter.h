#pragma once

#include <cstdint>

enum class CounterOverflowBehavior : uint8_t {
    Clamp,
    WrapAround,
};

enum class CounterDirection : uint8_t {
    Up,
    Down,
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
    [[nodiscard]] Counter(const CounterSettings<T>& settings);

    void stepUp();
    void stepDown();
    void step(CounterDirection direction);

    [[nodiscard]] auto getValue() const;
    void setStep(T step);
    [[nodiscard]] auto getStep() const;

private:
    T _step;
    T _value;
    T _minValue;
    T _maxValue;
    CounterOverflowBehavior _overflowBehavior;
};

#include "counter.ipp"
