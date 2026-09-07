#pragma once

#include <cstdint>
#include <bitset>
#include <array>

using StepIndex = uint8_t;
constexpr const StepIndex STEPS_COUNT = 16;

using MIDI_Note = uint8_t;
constexpr const uint8_t MIDI_OFFSET = 12;
constexpr const uint8_t NOTES_IN_OCTAVE = 12;

enum class Note : MIDI_Note {
    C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B,
};

struct SequencerStep {
    bool isEnabled;
    MIDI_Note note;

    void toggle() {
        isEnabled = !isEnabled;
    }

    void setNote(Note newNote, uint8_t octave) {
        setNote(static_cast<uint8_t>(newNote), octave);   
    }
    void setNote(uint8_t newNote, uint8_t octave) {
        note = MIDI_OFFSET + newNote + (NOTES_IN_OCTAVE * octave);
    }
};

class Sequencer {
public:
    Sequencer();

    void setBpm(uint8_t bpm);
    uint8_t getBpm() const;
    std::bitset<STEPS_COUNT> getStepsEnabled() const;
    StepIndex getCurrentStep() const;

    void toggleStep(StepIndex index);

    void sync(uint32_t micros);
    bool update(uint32_t micros);
    bool isCurrentStepEnabled() const;

private:
    std::array<SequencerStep, STEPS_COUNT>_steps{}; 
    StepIndex _currentStep;
    uint8_t _bpm;
    
    uint32_t _stepPeriodUs;
    uint32_t _lastStepAt = 0;

    uint32_t getStepPeriodUs() const;
};
