#include "sequencer.h"

#include <algorithm>

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

const std::array<SequencerStep, STEPS_COUNT>& Sequencer::steps() const { return _steps; }

void Sequencer::setSteps(const std::array<SequencerStep, STEPS_COUNT>& steps) { _steps = steps; }

std::bitset<STEPS_COUNT> Sequencer::getStepsEnabled() const {
    std::bitset<STEPS_COUNT> res{};

    for (StepIndex index = 0; index < STEPS_COUNT; index++) {
        res[index] = _steps[index].isEnabled;
    }

    return res;
}

StepIndex Sequencer::getCurrentStepIndex() const { return _currentStepIndex; }

std::optional<StepIndex> Sequencer::getDisplayStepIndex() const {
    if (!_hasCurrentStep) {
        return std::nullopt;
    }
    return _currentStepIndex;
}

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

bool Sequencer::adjustStepNote(StepIndex index, int16_t delta) {
    if (index >= STEPS_COUNT) {
        return false;
    }

    constexpr int minNote = MIDI_OFFSET;
    constexpr int maxNote = 127;
    const int next = static_cast<int>(_steps[index].note) + delta;
    _steps[index].note = static_cast<MIDI_Note>(std::clamp(next, minNote, maxNote));
    return true;
}

std::optional<uint8_t> Sequencer::getStepVelocity(StepIndex index) const {
    if (index >= STEPS_COUNT) {
        return std::nullopt;
    }
    return _steps[index].velocity;
}

bool Sequencer::adjustStepVelocity(StepIndex index, int8_t delta) {
    if (index >= STEPS_COUNT) {
        return false;
    }

    constexpr int minVelocity = 1;
    constexpr int maxVelocity = 127;
    const int next = static_cast<int>(_steps[index].velocity) + delta;
    _steps[index].velocity = static_cast<uint8_t>(std::clamp(next, minVelocity, maxVelocity));
    return true;
}

bool Sequencer::isRunning() const { return _running; }

std::optional<MIDI_Note> Sequencer::stop() {
    _running = false;
    const auto actual = _actualSoundingNote;
    _actualSoundingNote.reset();
    _projectedSoundingNote.reset();
    _scheduledBoundaryTick.reset();
    _scheduledSoundingNote.reset();
    return actual;
}

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

void Sequencer::start() {
    _running = true;
    _currentStepIndex = 0;
    _hasCurrentStep = false;
    _actualSoundingNote.reset();
    _projectedSoundingNote.reset();
    _nextBoundaryTick = 0;
    _scheduledBoundaryTick.reset();
    _scheduledSoundingNote.reset();
}

void Sequencer::continuePlayback() { _running = true; }

SwingMetro::MidiEventQueueEnqueueResult
Sequencer::scheduleThrough(SwingMetro::TransportPosition position,
                           SwingMetro::MidiEventQueue& queue) {
    if (!_running) {
        return SwingMetro::MidiEventQueueEnqueueResult::Ok;
    }
    const auto horizon = position.tick + SCHEDULING_LOOKAHEAD_TICKS;
    while (_nextBoundaryTick <= horizon) {
        const auto stepIndex = static_cast<StepIndex>(
            (_nextBoundaryTick / SwingMetro::kTicksPerSixteenth) % STEPS_COUNT);
        const auto& step = _steps[stepIndex];
        std::array<SwingMetro::MidiEventRequest, 2> requests{};
        std::size_t count = 0;
        if (_projectedSoundingNote.has_value()) {
            requests[count++] = {{_nextBoundaryTick, 0}, {0x08, 0x80, *_projectedSoundingNote, 0}};
        }
        std::optional<MIDI_Note> projected;
        if (step.isEnabled) {
            projected = step.note;
            requests[count++] = {{_nextBoundaryTick, 0}, {0x09, 0x90, step.note, step.velocity}};
        }
        const auto result = queue.enqueueBatch(requests, count);
        if (result != SwingMetro::MidiEventQueueEnqueueResult::Ok) {
            return result;
        }
        _currentStepIndex = stepIndex;
        _hasCurrentStep = true;
        _projectedSoundingNote = projected;
        _scheduledBoundaryTick = _nextBoundaryTick;
        _scheduledSoundingNote = projected;
        _nextBoundaryTick += SwingMetro::kTicksPerSixteenth;
    }
    return SwingMetro::MidiEventQueueEnqueueResult::Ok;
}

void Sequencer::notifyBoundaryReached(SwingMetro::TransportTick tick) {
    if (_scheduledBoundaryTick == tick) {
        _actualSoundingNote = _scheduledSoundingNote;
    }
}

std::optional<MIDI_Note> Sequencer::actualSoundingNote() const { return _actualSoundingNote; }

void Sequencer::sync(uint32_t micros) {
    _lastStepAt = micros - _stepPeriodUs;
    _currentStepIndex = STEPS_COUNT - 1;
    _hasCurrentStep = false;
}

bool Sequencer::update(uint32_t micros) {
    if (!_running) {
        return false;
    }
    if (micros - _lastStepAt >= _stepPeriodUs) {
        _lastStepAt += _stepPeriodUs;
        _currentStepIndex = (_currentStepIndex + 1) % STEPS_COUNT;
        _hasCurrentStep = true;

        return true;
    }

    return false;
}

void Sequencer::externalStart() {
    start();
    _currentStepIndex = STEPS_COUNT - 1;
}

void Sequencer::externalContinue() { continuePlayback(); }

void Sequencer::externalStop() { (void)stop(); }

bool Sequencer::advanceExternal() {
    if (!_running) {
        return false;
    }
    _currentStepIndex = (_currentStepIndex + 1) % STEPS_COUNT;
    _hasCurrentStep = true;
    return true;
}

bool Sequencer::isCurrentStepEnabled() const { return _steps[_currentStepIndex].isEnabled; }

uint8_t Sequencer::currentStepMidiNote() const { return _steps[_currentStepIndex].note; }

uint8_t Sequencer::currentStepVelocity() const { return _steps[_currentStepIndex].velocity; }
