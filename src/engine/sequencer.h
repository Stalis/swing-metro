#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>

#include "sequencer_step.h"
#include "step_planner.h"

class Sequencer {
  public:
    Sequencer();

    void setBpm(uint8_t bpm);
    [[nodiscard]] uint8_t getBpm() const;
    void setSwing(uint8_t swing);
    [[nodiscard]] uint8_t getSwing() const;
    [[nodiscard]] const std::array<SequencerStep, STEPS_COUNT>& steps() const;
    void setSteps(const std::array<SequencerStep, STEPS_COUNT>& steps);
    [[nodiscard]] std::bitset<STEPS_COUNT> getStepsEnabled() const;
    [[nodiscard]] StepIndex getCurrentStepIndex() const;
    [[nodiscard]] std::optional<StepIndex> getDisplayStepIndex() const;

    void toggleStep(StepIndex index);
    [[nodiscard]] std::optional<MIDI_Note> getStepMidiNote(StepIndex index) const;
    bool adjustStepNote(StepIndex index, int16_t delta);
    [[nodiscard]] std::optional<uint8_t> getStepVelocity(StepIndex index) const;
    bool adjustStepVelocity(StepIndex index, int8_t delta);
    [[nodiscard]] std::optional<uint8_t> getStepGate(StepIndex index) const;
    bool adjustStepGate(StepIndex index, int8_t delta);

    [[nodiscard]] bool isRunning() const;
    std::optional<MIDI_Note> stop();
    void toggleRunning(uint32_t micros);

    void start();
    void continuePlayback();
    [[nodiscard]] SwingMetro::MidiEventQueueEnqueueResult
    scheduleThrough(SwingMetro::TransportPosition position, SwingMetro::MidiEventQueue& queue);
    void notifyBoundaryReached(SwingMetro::TransportTick tick);
    void notifyNoteOnAccepted(MIDI_Note note);
    void notifyNoteOffAccepted(MIDI_Note note);
    void notifyNoteOnQueued(const NoteLaunch& launch) noexcept;
    void notifyNoteOnExpired(const NoteLaunch& launch) noexcept;
    void notifyNoteOnAccepted(const NoteLaunch& launch);
    void notifyNoteOffAccepted(const NoteLaunch& launch);
    void cancelRequestedNoteOff() noexcept;
    void abandonRemoteNoteState() noexcept;
    [[nodiscard]] std::optional<MIDI_Note> actualSoundingNote() const;
    [[nodiscard]] std::optional<NoteLaunch> actualSoundingLaunch() const noexcept;
    [[nodiscard]] std::optional<NoteLaunch> projectedOrActualLaunch() const noexcept;
    [[nodiscard]] RemoteNoteState remoteNoteState() const noexcept;
    void beginCleanRemoteSession(std::uint32_t generation = 0) noexcept;
    void setSessionGeneration(std::uint32_t generation) noexcept;

    void sync(uint32_t micros);
    bool update(uint32_t micros);
    void externalStart();
    void externalContinue();
    void externalStop();
    bool advanceExternal();

    bool isCurrentStepEnabled() const;
    uint8_t currentStepMidiNote() const;
    uint8_t currentStepVelocity() const;

  private:
    std::array<SequencerStep, STEPS_COUNT> _steps{};
    StepIndex _currentStepIndex;
    bool _hasCurrentStep = false;
    uint8_t _bpm;
    uint8_t _swing = SwingMetro::SWING_MIN_VALUE;
    bool _running = false;
    NoteLifecycle _noteLifecycle;
    StepPlanner _stepPlanner;

    uint32_t _stepPeriodUs;
    uint32_t _lastStepAt = 0;

    uint32_t getStepPeriodUs() const;
};
