#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>

using StepIndex = uint8_t;
constexpr const StepIndex STEPS_COUNT = 16;

using MIDI_Note = uint8_t;
constexpr const uint8_t NOTES_IN_OCTAVE = 12;
constexpr const uint8_t MIDI_OFFSET = NOTES_IN_OCTAVE * 3;

enum class Note : MIDI_Note {
    C,
    Cs,
    D,
    Ds,
    E,
    F,
    Fs,
    G,
    Gs,
    A,
    As,
    B,
};

inline uint8_t getNote(uint8_t newNote, uint8_t octave) {
    return MIDI_OFFSET + newNote + (NOTES_IN_OCTAVE * octave);
}

inline uint8_t getNote(Note newNote, uint8_t octave) {
    return getNote(static_cast<uint8_t>(newNote), octave);
}

struct SequencerStep {
    bool isEnabled = false;
    MIDI_Note note = getNote(Note::C, 0);
    uint8_t velocity = 127;

    void toggle() { isEnabled = !isEnabled; }

    void setNote(Note newNote, uint8_t octave) { note = getNote(newNote, octave); }
    void setNote(uint8_t newNote, uint8_t octave) { note = getNote(newNote, octave); }
};

class Sequencer {
  public:
    Sequencer();

    void setBpm(uint8_t bpm);
    [[nodiscard]] uint8_t getBpm() const;
    [[nodiscard]] std::bitset<STEPS_COUNT> getStepsEnabled() const;
    [[nodiscard]] StepIndex getCurrentStepIndex() const;

    void toggleStep(StepIndex index);
    [[nodiscard]] std::optional<MIDI_Note> getStepMidiNote(StepIndex index) const;
    bool adjustStepNote(StepIndex index, int8_t delta);

    [[nodiscard]] bool isRunning() const;
    void toggleRunning(uint32_t micros);

    void sync(uint32_t micros);
    bool update(uint32_t micros);

    bool isCurrentStepEnabled() const;
    uint8_t currentStepMidiNote() const;
    uint8_t currentStepVelocity() const;

  private:
    std::array<SequencerStep, STEPS_COUNT> _steps{};
    StepIndex _currentStepIndex;
    uint8_t _bpm;
    bool _running = true;

    uint32_t _stepPeriodUs;
    uint32_t _lastStepAt = 0;

    uint32_t getStepPeriodUs() const;
};
