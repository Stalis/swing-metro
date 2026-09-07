#include "sequencer.h"

constexpr uint8_t DEFAULT_BPM = 120;
constexpr uint8_t MIN_BPM = 40;
constexpr uint8_t MAX_BPM = 240;

constexpr uint8_t clampBpm(uint8_t bpm) {
    if (bpm <= MIN_BPM) {
        return MIN_BPM;
    }
    if (bpm >= MAX_BPM) {
        return MAX_BPM;
    }
    return bpm;
}

Sequencer::Sequencer()
    : _currentStep(0), _bpm(DEFAULT_BPM), _stepPeriodUs(getStepPeriodUs()), _lastStepAt(0)
 {}

uint8_t Sequencer::getBpm() const {
    return _bpm;
}

std::bitset<STEPS_COUNT> Sequencer::getStepsEnabled() const { 
    std::bitset<STEPS_COUNT> res{};

    for (StepIndex index = 0; index < STEPS_COUNT; index++) {
        res[index] = _steps[index].isEnabled;
    }

    return res; 
}

StepIndex Sequencer::getCurrentStep() const { return _currentStep; }

void Sequencer::setBpm(uint8_t bpm) {
    if (bpm == _bpm) {
        return;
    }

    _bpm = clampBpm(bpm);
    _stepPeriodUs = getStepPeriodUs();
}

void Sequencer::toggleStep(StepIndex index) {
    _steps[index].toggle();
}

uint32_t Sequencer::getStepPeriodUs() const {
    constexpr uint32_t MICROSECONDS_PER_MINUTE = 60'000'000;
    constexpr uint8_t STEPS_PER_QUARTER = 4;

    return MICROSECONDS_PER_MINUTE / (static_cast<uint32_t>(_bpm) * STEPS_PER_QUARTER);
}

void Sequencer::sync(uint32_t micros) {
    _lastStepAt = micros;
    _currentStep = STEPS_COUNT - 1;
}

bool Sequencer::update(uint32_t micros) {
    if (micros - _lastStepAt >= _stepPeriodUs) {
        _lastStepAt += _stepPeriodUs;
        _currentStep = (_currentStep + 1) % STEPS_COUNT;

        return true;
    }

    return false;
}

bool Sequencer::isCurrentStepEnabled() const {
    return _steps[_currentStep].isEnabled;
}