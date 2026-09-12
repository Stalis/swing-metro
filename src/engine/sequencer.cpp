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
    : _currentStepIndex(0), _bpm(DEFAULT_BPM), _stepPeriodUs(getStepPeriodUs()), _lastStepAt(0) {}

uint8_t Sequencer::getBpm() const { return _bpm; }

std::bitset<STEPS_COUNT> Sequencer::getStepsEnabled() const {
    std::bitset<STEPS_COUNT> res{};

    for (StepIndex index = 0; index < STEPS_COUNT; index++) {
        res[index] = _steps[index].isEnabled;
    }

    return res;
}

StepIndex Sequencer::getCurrentStepIndex() const { return _currentStepIndex; }

void Sequencer::setBpm(uint8_t bpm) {
    if (bpm == _bpm) {
        return;
    }

    _bpm = clampBpm(bpm);
    _stepPeriodUs = getStepPeriodUs();
}

void Sequencer::toggleStep(StepIndex index) { _steps[index].toggle(); }

std::optional<MIDI_Note> Sequencer::getStepMidiNote(StepIndex index) const {
    if (index >= STEPS_COUNT) {
        return std::nullopt;
    }
    return _steps[index].note;
}

bool Sequencer::adjustStepNote(StepIndex index, int8_t delta) {
    if (index >= STEPS_COUNT) {
        return false;
    }

    constexpr int minNote = MIDI_OFFSET;
    constexpr int maxNote = 127;
    const int next = static_cast<int>(_steps[index].note) + delta;
    _steps[index].note = static_cast<MIDI_Note>(next < minNote   ? minNote
                                                : next > maxNote ? maxNote
                                                                 : next);
    return true;
}

bool Sequencer::isRunning() const { return _running; }

void Sequencer::toggleRunning(uint32_t micros) {
    _running = !_running;
    if (_running) {
        sync(micros);
    }
}

uint32_t Sequencer::getStepPeriodUs() const {
    constexpr uint32_t MICROSECONDS_PER_MINUTE = 60'000'000;
    constexpr uint8_t STEPS_PER_QUARTER = 4;

    return MICROSECONDS_PER_MINUTE / (static_cast<uint32_t>(_bpm) * STEPS_PER_QUARTER);
}

void Sequencer::sync(uint32_t micros) {
    _lastStepAt = micros;
    _currentStepIndex = STEPS_COUNT - 1;
}

bool Sequencer::update(uint32_t micros) {
    if (!_running) {
        return false;
    }
    if (micros - _lastStepAt >= _stepPeriodUs) {
        _lastStepAt += _stepPeriodUs;
        _currentStepIndex = (_currentStepIndex + 1) % STEPS_COUNT;

        return true;
    }

    return false;
}

bool Sequencer::isCurrentStepEnabled() const { return _steps[_currentStepIndex].isEnabled; }

uint8_t Sequencer::currentStepMidiNote() const { return _steps[_currentStepIndex].note; }

uint8_t Sequencer::currentStepVelocity() const { return _steps[_currentStepIndex].velocity; }
