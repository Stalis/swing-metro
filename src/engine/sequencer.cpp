#include "sequencer.h"

#include <algorithm>
#include <limits>

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

void Sequencer::setSwing(uint8_t swing) { _swing = SwingMetro::clampSwingValue(swing); }

uint8_t Sequencer::getSwing() const { return _swing; }

const std::array<SequencerStep, STEPS_COUNT>& Sequencer::steps() const { return _steps; }

void Sequencer::setSteps(const std::array<SequencerStep, STEPS_COUNT>& steps) {
    _steps = steps;
    for (auto& step : _steps) {
        step.gate = std::clamp(step.gate, STEP_MIN_GATE, STEP_MAX_GATE);
    }
}

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

std::optional<uint8_t> Sequencer::getStepGate(StepIndex index) const {
    if (index >= STEPS_COUNT) {
        return std::nullopt;
    }
    return _steps[index].gate;
}

bool Sequencer::adjustStepGate(StepIndex index, int8_t delta) {
    if (index >= STEPS_COUNT) {
        return false;
    }

    const int next = static_cast<int>(_steps[index].gate) + delta;
    _steps[index].gate = static_cast<uint8_t>(
        std::clamp(next, static_cast<int>(STEP_MIN_GATE), static_cast<int>(STEP_MAX_GATE)));
    return true;
}

bool Sequencer::isRunning() const { return _running; }

std::optional<MIDI_Note> Sequencer::stop() {
    _running = false;
    if (_actualSoundingLaunch.has_value() &&
        (!_requestedNoteOff.has_value() ||
         _requestedNoteOff->launchId != _actualSoundingLaunch->launchId ||
         _requestedNoteOff->sessionGeneration != _actualSoundingLaunch->sessionGeneration)) {
        _requestedNoteOff = _actualSoundingLaunch;
        return _actualSoundingLaunch->note;
    }
    return std::nullopt;
}

void Sequencer::toggleRunning(uint32_t micros) {
    _running = !_running;
    if (_running) {
        sync(micros);
    }
}

uint32_t Sequencer::getStepPeriodUs() const {
    constexpr uint32_t microsecondsPerMinute = 60'000'000;
    constexpr uint8_t stepsPerQuarter = 4;

    return microsecondsPerMinute / (static_cast<uint32_t>(_bpm) * stepsPerQuarter);
}

void Sequencer::start() {
    _running = true;
    _currentStepIndex = 0;
    _hasCurrentStep = false;
    _nextBoundaryTick = 0;
    _nextLaunchId = 1;
    _schedulingComplete = false;
}

void Sequencer::continuePlayback() { _running = true; }

SwingMetro::MidiEventQueueEnqueueResult
Sequencer::scheduleThrough(SwingMetro::TransportPosition position,
                           SwingMetro::MidiEventQueue& queue) {
    if (!_running || _schedulingComplete) {
        return SwingMetro::MidiEventQueueEnqueueResult::Ok;
    }
    const auto horizon = position.tick > std::numeric_limits<SwingMetro::TransportTick>::max() -
                                             SCHEDULING_LOOKAHEAD_TICKS
                             ? std::numeric_limits<SwingMetro::TransportTick>::max()
                             : position.tick + SCHEDULING_LOOKAHEAD_TICKS;
    while (_nextBoundaryTick <= horizon) {
        const auto stepIndex = static_cast<StepIndex>(
            (_nextBoundaryTick / SwingMetro::TICKS_PER_SIXTEENTH) % STEPS_COUNT);
        const auto& step = _steps[stepIndex];
        if (step.isEnabled) {
            const SwingMetro::TransportPosition onPosition{
                _nextBoundaryTick, SwingMetro::swingPhase(stepIndex, _swing)};
            const auto launchId = _nextLaunchId;
            const auto offPosition = SwingMetro::gateDeadline(onPosition, step.gate);
            const std::array<SwingMetro::MidiEventRequest, 2> requests = {
                SwingMetro::MidiEventRequest{
                    onPosition, *SwingMetro::MidiMessage::noteOn(0, step.note, step.velocity),
                    launchId, _sessionGeneration, offPosition},
                SwingMetro::MidiEventRequest{offPosition,
                                             *SwingMetro::MidiMessage::noteOff(0, step.note),
                                             launchId, _sessionGeneration, offPosition},
            };
            const auto result = queue.enqueueBatch(requests, requests.size());
            if (result != SwingMetro::MidiEventQueueEnqueueResult::Ok) {
                return result;
            }
            ++_nextLaunchId;
            if (_nextLaunchId == 0) {
                ++_nextLaunchId;
            }
        }
        if (_nextBoundaryTick > std::numeric_limits<SwingMetro::TransportTick>::max() -
                                    SwingMetro::TICKS_PER_SIXTEENTH) {
            _schedulingComplete = true;
            break;
        }
        _nextBoundaryTick += SwingMetro::TICKS_PER_SIXTEENTH;
    }
    return SwingMetro::MidiEventQueueEnqueueResult::Ok;
}

void Sequencer::notifyBoundaryReached(SwingMetro::TransportTick tick) {
    _currentStepIndex =
        static_cast<StepIndex>((tick / SwingMetro::TICKS_PER_SIXTEENTH) % STEPS_COUNT);
    _hasCurrentStep = true;
}

void Sequencer::notifyNoteOnAccepted(MIDI_Note note) {
    notifyNoteOnAccepted({note, 0, _sessionGeneration});
}

void Sequencer::notifyNoteOnAccepted(const NoteLaunch& launch) {
    _actualSoundingLaunch = launch;
    if (!_projectedLaunch.has_value() || _projectedLaunch->launchId == launch.launchId) {
        _projectedLaunch = launch;
    }
    _requestedNoteOff.reset();
}

void Sequencer::notifyNoteOnQueued(const NoteLaunch& launch) noexcept { _projectedLaunch = launch; }

void Sequencer::notifyNoteOnExpired(const NoteLaunch& launch) noexcept {
    if (_projectedLaunch.has_value() && _projectedLaunch->launchId == launch.launchId &&
        _projectedLaunch->sessionGeneration == launch.sessionGeneration) {
        _projectedLaunch.reset();
    }
}

void Sequencer::notifyNoteOffAccepted(MIDI_Note note) {
    notifyNoteOffAccepted({note, 0, _sessionGeneration});
}

void Sequencer::notifyNoteOffAccepted(const NoteLaunch& launch) {
    const auto matches = [&launch](const std::optional<NoteLaunch>& candidate) {
        return candidate.has_value() && candidate->note == launch.note &&
               (launch.launchId == 0 || candidate->launchId == launch.launchId) &&
               (launch.sessionGeneration == 0 || candidate->sessionGeneration == 0 ||
                candidate->sessionGeneration == launch.sessionGeneration);
    };
    if (matches(_actualSoundingLaunch)) {
        _actualSoundingLaunch.reset();
    }
    if (matches(_requestedNoteOff)) {
        _requestedNoteOff.reset();
    }
    if (matches(_projectedLaunch)) {
        _projectedLaunch.reset();
    }
}

void Sequencer::cancelRequestedNoteOff() noexcept { _requestedNoteOff.reset(); }

void Sequencer::abandonRemoteNoteState() noexcept {
    _actualSoundingLaunch.reset();
    _projectedLaunch.reset();
    _requestedNoteOff.reset();
    _remoteNoteState = RemoteNoteState::Unknown;
}

std::optional<MIDI_Note> Sequencer::actualSoundingNote() const {
    return _actualSoundingLaunch.has_value() ? std::optional<MIDI_Note>{_actualSoundingLaunch->note}
                                             : std::nullopt;
}

std::optional<NoteLaunch> Sequencer::actualSoundingLaunch() const noexcept {
    return _actualSoundingLaunch;
}

std::optional<NoteLaunch> Sequencer::projectedOrActualLaunch() const noexcept {
    return _projectedLaunch.has_value() ? _projectedLaunch : _actualSoundingLaunch;
}

RemoteNoteState Sequencer::remoteNoteState() const noexcept { return _remoteNoteState; }

void Sequencer::beginCleanRemoteSession(std::uint32_t generation) noexcept {
    _actualSoundingLaunch.reset();
    _projectedLaunch.reset();
    _requestedNoteOff.reset();
    _sessionGeneration = generation;
    _remoteNoteState = RemoteNoteState::Clean;
}

void Sequencer::setSessionGeneration(std::uint32_t generation) noexcept {
    _sessionGeneration = generation;
}

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
