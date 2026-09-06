template <typename T>
Counter<T>::Counter(const CounterSettings<T>& settings)
    : _step(settings.step), _value(settings.value), _minValue(settings.minValue),
      _maxValue(settings.maxValue), _overflowBehavior(settings.overflowBehavior) {}

template <typename T>
void Counter<T>::stepUp() {
    step(CounterDirection::Up);
}

template <typename T>
void Counter<T>::stepDown() {
    step(CounterDirection::Down);
}

template <typename T>
void Counter<T>::step(CounterDirection direction) {
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

template <typename T>
auto Counter<T>::getValue() const {
    return _value;
}

template <typename T>
void Counter<T>::setStep(T step) {
    _step = step;
}

template <typename T>
auto Counter<T>::getStep() const {
    return _step;
}
