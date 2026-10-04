#pragma once

#include "engine/external_midi_clock.h"
#include "engine/midi_clock_mode.h"
#include "program/program_slot_store.h"
#include "program/program_storage_modal.h"

#include <array>
#include <bitset>
#include <cstdint>

enum class UiPage : uint8_t {
    MainDisplay,
    StepSettings,
};

enum class UiStepMode : uint8_t { Normal, Legato, Repeat };

struct UiSettings {
    struct Main {
        uint8_t tempo;
        uint8_t swing;
        uint8_t volume;
        uint8_t activeNote;
        std::bitset<16> notesState;
        std::array<uint8_t, 16> stepNotes{};
        std::array<uint8_t, 16> stepVelocities{};
        std::array<uint8_t, 16> stepGates{};
        uint8_t sequenceNumber = 1;
        uint8_t sequenceLength = 16;
        uint16_t midiChannelMask = 0xFFFF;
        uint8_t highlightedStep = UINT8_MAX;
        SwingMetro::MidiClockMode clockMode = SwingMetro::MidiClockMode::Off;
        SwingMetro::ExternalMidiClockStatus externalClockStatus =
            SwingMetro::ExternalMidiClockStatus::Waiting;
        uint8_t externalTempo = 0;
    } main;

    struct Editor {
        uint8_t selectedStep = UINT8_MAX;
        uint8_t selectedNote = 36;
        uint8_t selectedVelocity = 127;
        uint8_t selectedGate = 100;
        UiStepMode mode = UiStepMode::Normal;
        uint8_t repeatCount = 4;
        bool transportRunning = true;
        bool shiftActive = false;
    } editor;

    struct MidiClock {
        bool modalOpen = false;
        SwingMetro::MidiClockMode active = SwingMetro::MidiClockMode::Off;
        SwingMetro::MidiClockMenuItem selection = SwingMetro::MidiClockMenuItem::Off;
    } midiClock;

    struct Storage {
        SwingMetro::ProgramStorageModalState state = SwingMetro::ProgramStorageModalState::Closed;
        SwingMetro::ProgramStorageMenuItem selection = SwingMetro::ProgramStorageMenuItem::Save;
        SwingMetro::ProgramStorageAction action = SwingMetro::ProgramStorageAction::Save;
        uint8_t slot = 0;
        SwingMetro::ProgramResetChoice resetChoice = SwingMetro::ProgramResetChoice::No;
        SwingMetro::ProgramStoreStatus status = SwingMetro::ProgramStoreStatus::Ok;
    } storage;

    UiPage page = UiPage::MainDisplay;
};
