#pragma once

#include <cstdint>

#include "note_lifecycle.h"

using StepIndex = uint8_t;
constexpr const StepIndex STEPS_COUNT = 16;

constexpr const uint8_t NOTES_IN_OCTAVE = 12;
constexpr const uint8_t MIDI_OFFSET = NOTES_IN_OCTAVE * 3;
constexpr const uint8_t STEP_DEFAULT_GATE = 100;
constexpr const uint8_t STEP_MIN_GATE = 1;
constexpr const uint8_t STEP_MAX_GATE = 100;

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
    uint8_t gate = STEP_DEFAULT_GATE;

    void toggle() { isEnabled = !isEnabled; }

    void setNote(Note newNote, uint8_t octave) { note = getNote(newNote, octave); }
    void setNote(uint8_t newNote, uint8_t octave) { note = getNote(newNote, octave); }
};

[[nodiscard]] constexpr bool isValid(const SequencerStep& step) {
    return step.gate >= STEP_MIN_GATE && step.gate <= STEP_MAX_GATE;
}
