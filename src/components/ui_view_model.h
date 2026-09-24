#pragma once

#include "engine/external_midi_clock.h"
#include "engine/midi_clock_mode.h"
#include "program/program_slot_store.h"
#include "program/program_storage_modal.h"

#include <atomic>
#include <bitset>
#include <cstdint>
#include <optional>

enum class UiPage : uint8_t {
    MainDisplay,
    StepSettings,
};

struct UiSettings {
    uint8_t tempo;
    uint8_t swing;
    uint8_t volume;
    uint8_t activeNote;

    std::bitset<16> notesState;
    UiPage page = UiPage::MainDisplay;
    uint8_t selectedStep = UINT8_MAX;
    uint8_t selectedNote = 36;
    uint8_t selectedVelocity = 127;
    uint8_t selectedGate = 100;
    bool transportRunning = true;
    bool shiftActive = false;
    bool midiClockModalOpen = false;
    SwingMetro::MidiClockMode midiClockActive = SwingMetro::MidiClockMode::Off;
    SwingMetro::MidiClockMode midiClockPreview = SwingMetro::MidiClockMode::Off;
    SwingMetro::ExternalMidiClockStatus externalClockStatus =
        SwingMetro::ExternalMidiClockStatus::Waiting;
    uint8_t externalTempo = 0;
    SwingMetro::ProgramStorageModalState programStorageState =
        SwingMetro::ProgramStorageModalState::Closed;
    SwingMetro::ProgramStorageAction programStorageAction = SwingMetro::ProgramStorageAction::Save;
    uint8_t programStorageSlot = 0;
    SwingMetro::ProgramStoreStatus programStorageStatus = SwingMetro::ProgramStoreStatus::Ok;
};

class UiViewModel {
  public:
    // Only the producer core writes; readers retry if a publication overlaps.
    void publish(UiSettings settings) {
        if (_lastPublished.has_value() && sameSettings(*_lastPublished, settings)) {
            return;
        }

        const uint32_t packed = static_cast<uint32_t>(settings.tempo) |
                                static_cast<uint32_t>(settings.swing) << 8 |
                                static_cast<uint32_t>(settings.volume) << 16 |
                                static_cast<uint32_t>(settings.activeNote) << (16 + 8);
        const uint16_t notesPacked = static_cast<uint16_t>(settings.notesState.to_ulong());
        const uint32_t navigationPacked = static_cast<uint32_t>(settings.selectedStep) |
                                          static_cast<uint32_t>(settings.selectedNote) << 8 |
                                          static_cast<uint32_t>(settings.page) << 16 |
                                          static_cast<uint32_t>(settings.transportRunning) << 17 |
                                          static_cast<uint32_t>(settings.shiftActive) << 18 |
                                          static_cast<uint32_t>(settings.selectedVelocity) << 19 |
                                          static_cast<uint32_t>(settings.midiClockModalOpen) << 27 |
                                          static_cast<uint32_t>(settings.midiClockActive) << 28 |
                                          static_cast<uint32_t>(settings.midiClockPreview) << 30;

        _generation.fetch_add(1, std::memory_order_seq_cst);
        _packed.store(packed, std::memory_order_seq_cst);
        _notesPacked.store(notesPacked, std::memory_order_seq_cst);
        _navigationPacked.store(navigationPacked, std::memory_order_seq_cst);
        _selectedGate.store(settings.selectedGate, std::memory_order_seq_cst);
        _externalClockPacked.store(static_cast<uint32_t>(settings.externalTempo) |
                                       static_cast<uint32_t>(settings.externalClockStatus) << 8,
                                   std::memory_order_seq_cst);
        _programStoragePacked.store(static_cast<uint32_t>(settings.programStorageState) |
                                        static_cast<uint32_t>(settings.programStorageAction) << 3 |
                                        static_cast<uint32_t>(settings.programStorageSlot) << 4 |
                                        static_cast<uint32_t>(settings.programStorageStatus) << 8,
                                    std::memory_order_seq_cst);
        _generation.fetch_add(1, std::memory_order_seq_cst);
        _lastPublished = settings;
    }

    [[nodiscard]] UiSettings read() const {
        for (;;) {
            const uint32_t before = _generation.load(std::memory_order_seq_cst);
            if ((before & 1U) != 0) {
                continue;
            }

            const uint32_t packed = _packed.load(std::memory_order_seq_cst);
            const uint16_t notesPacked = _notesPacked.load(std::memory_order_seq_cst);
            const uint32_t navigationPacked = _navigationPacked.load(std::memory_order_seq_cst);
            const uint8_t selectedGate = _selectedGate.load(std::memory_order_seq_cst);
            const uint32_t externalClockPacked =
                _externalClockPacked.load(std::memory_order_seq_cst);
            const uint32_t programStoragePacked =
                _programStoragePacked.load(std::memory_order_seq_cst);
            const uint32_t after = _generation.load(std::memory_order_seq_cst);
            if (before != after) {
                continue;
            }

            return {
                .tempo = static_cast<uint8_t>(packed),
                .swing = static_cast<uint8_t>(packed >> 8),
                .volume = static_cast<uint8_t>(packed >> 16),
                .activeNote = static_cast<uint8_t>(packed >> 24),
                .notesState = std::bitset<16>(notesPacked),
                .page = static_cast<UiPage>((navigationPacked >> 16) & 1U),
                .selectedStep = static_cast<uint8_t>(navigationPacked),
                .selectedNote = static_cast<uint8_t>(navigationPacked >> 8),
                .selectedVelocity = static_cast<uint8_t>(navigationPacked >> 19),
                .selectedGate = selectedGate,
                .transportRunning = ((navigationPacked >> 17) & 1U) != 0,
                .shiftActive = ((navigationPacked >> 18) & 1U) != 0,
                .midiClockModalOpen = ((navigationPacked >> 27) & 1U) != 0,
                .midiClockActive =
                    static_cast<SwingMetro::MidiClockMode>((navigationPacked >> 28) & 3U),
                .midiClockPreview = static_cast<SwingMetro::MidiClockMode>(navigationPacked >> 30),
                .externalClockStatus = static_cast<SwingMetro::ExternalMidiClockStatus>(
                    (externalClockPacked >> 8) & 3U),
                .externalTempo = static_cast<uint8_t>(externalClockPacked),
                .programStorageState =
                    static_cast<SwingMetro::ProgramStorageModalState>(programStoragePacked & 7U),
                .programStorageAction =
                    static_cast<SwingMetro::ProgramStorageAction>((programStoragePacked >> 3) & 1U),
                .programStorageSlot = static_cast<uint8_t>((programStoragePacked >> 4) & 15U),
                .programStorageStatus =
                    static_cast<SwingMetro::ProgramStoreStatus>(programStoragePacked >> 8),
            };
        }
    }

  private:
    [[nodiscard]] static bool sameSettings(const UiSettings& left, const UiSettings& right) {
        return left.tempo == right.tempo && left.swing == right.swing &&
               left.volume == right.volume && left.activeNote == right.activeNote &&
               left.notesState == right.notesState && left.page == right.page &&
               left.selectedStep == right.selectedStep && left.selectedNote == right.selectedNote &&
               left.selectedVelocity == right.selectedVelocity &&
               left.selectedGate == right.selectedGate &&
               left.transportRunning == right.transportRunning &&
               left.shiftActive == right.shiftActive &&
               left.midiClockModalOpen == right.midiClockModalOpen &&
               left.midiClockActive == right.midiClockActive &&
               left.midiClockPreview == right.midiClockPreview &&
               left.externalClockStatus == right.externalClockStatus &&
               left.externalTempo == right.externalTempo &&
               left.programStorageState == right.programStorageState &&
               left.programStorageAction == right.programStorageAction &&
               left.programStorageSlot == right.programStorageSlot &&
               left.programStorageStatus == right.programStorageStatus;
    }

    std::atomic<uint32_t> _packed{0};
    std::atomic<uint16_t> _notesPacked{0};
    std::atomic<uint32_t> _navigationPacked{0};
    std::atomic<uint8_t> _selectedGate{100};
    std::atomic<uint32_t> _externalClockPacked{0};
    std::atomic<uint32_t> _programStoragePacked{0};
    std::atomic<uint32_t> _generation{0};
    std::optional<UiSettings> _lastPublished;
};
