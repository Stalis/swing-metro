#pragma once

#include "ui_snapshot.h"

#include <atomic>
#include <cstdint>
#include <optional>

class UiViewModel {
  public:
    // Only the producer core writes; readers retry if a publication overlaps.
    void publish(UiSettings settings) {
        if (_lastPublished.has_value() && sameSettings(*_lastPublished, settings)) {
            return;
        }

        const uint32_t packed = static_cast<uint32_t>(settings.main.tempo) |
                                static_cast<uint32_t>(settings.main.swing) << 8 |
                                static_cast<uint32_t>(settings.main.volume) << 16 |
                                static_cast<uint32_t>(settings.main.activeNote) << (16 + 8);
        const uint16_t notesPacked = static_cast<uint16_t>(settings.main.notesState.to_ulong());
        const uint32_t navigationPacked =
            static_cast<uint32_t>(settings.editor.selectedStep) |
            static_cast<uint32_t>(settings.editor.selectedNote) << 8 |
            static_cast<uint32_t>(settings.page) << 16 |
            static_cast<uint32_t>(settings.editor.transportRunning) << 17 |
            static_cast<uint32_t>(settings.editor.shiftActive) << 18 |
            static_cast<uint32_t>(settings.editor.selectedVelocity) << 19 |
            static_cast<uint32_t>(settings.midiClock.modalOpen) << 27 |
            static_cast<uint32_t>(settings.midiClock.active) << 28 |
            static_cast<uint32_t>(settings.midiClock.selection) << 30;

        _generation.fetch_add(1, std::memory_order_seq_cst);
        _packed.store(packed, std::memory_order_seq_cst);
        _notesPacked.store(notesPacked, std::memory_order_seq_cst);
        _navigationPacked.store(navigationPacked, std::memory_order_seq_cst);
        _selectedGate.store(settings.editor.selectedGate, std::memory_order_seq_cst);
        _externalClockPacked.store(static_cast<uint32_t>(settings.main.externalTempo) |
                                       static_cast<uint32_t>(settings.main.externalClockStatus)
                                           << 8,
                                   std::memory_order_seq_cst);
        _programStoragePacked.store(static_cast<uint32_t>(settings.storage.state) |
                                        static_cast<uint32_t>(settings.storage.action) << 3 |
                                        static_cast<uint32_t>(settings.storage.selection) << 4 |
                                        static_cast<uint32_t>(settings.storage.slot) << 6 |
                                        static_cast<uint32_t>(settings.storage.resetChoice) << 14 |
                                        static_cast<uint32_t>(settings.storage.status) << 15,
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
                .main = {.tempo = static_cast<uint8_t>(packed),
                         .swing = static_cast<uint8_t>(packed >> 8),
                         .volume = static_cast<uint8_t>(packed >> 16),
                         .activeNote = static_cast<uint8_t>(packed >> 24),
                         .notesState = std::bitset<16>(notesPacked),
                         .externalClockStatus = static_cast<SwingMetro::ExternalMidiClockStatus>(
                             (externalClockPacked >> 8) & 3U),
                         .externalTempo = static_cast<uint8_t>(externalClockPacked)},
                .editor = {.selectedStep = static_cast<uint8_t>(navigationPacked),
                           .selectedNote = static_cast<uint8_t>(navigationPacked >> 8),
                           .selectedVelocity = static_cast<uint8_t>(navigationPacked >> 19),
                           .selectedGate = selectedGate,
                           .transportRunning = ((navigationPacked >> 17) & 1U) != 0,
                           .shiftActive = ((navigationPacked >> 18) & 1U) != 0},
                .midiClock = {.modalOpen = ((navigationPacked >> 27) & 1U) != 0,
                              .active = static_cast<SwingMetro::MidiClockMode>(
                                  (navigationPacked >> 28) & 3U),
                              .selection = static_cast<SwingMetro::MidiClockMenuItem>(
                                  navigationPacked >> 30)},
                .storage = {.state = static_cast<SwingMetro::ProgramStorageModalState>(
                                programStoragePacked & 7U),
                            .selection = static_cast<SwingMetro::ProgramStorageMenuItem>(
                                (programStoragePacked >> 4) & 3U),
                            .action = static_cast<SwingMetro::ProgramStorageAction>(
                                (programStoragePacked >> 3) & 1U),
                            .slot = static_cast<uint8_t>((programStoragePacked >> 6) & 255U),
                            .resetChoice = static_cast<SwingMetro::ProgramResetChoice>(
                                (programStoragePacked >> 14) & 1U),
                            .status = static_cast<SwingMetro::ProgramStoreStatus>(
                                programStoragePacked >> 15)},
                .page = static_cast<UiPage>((navigationPacked >> 16) & 1U),
            };
        }
    }

  private:
    [[nodiscard]] static bool sameSettings(const UiSettings& left, const UiSettings& right) {
        return left.main.tempo == right.main.tempo && left.main.swing == right.main.swing &&
               left.main.volume == right.main.volume &&
               left.main.activeNote == right.main.activeNote &&
               left.main.notesState == right.main.notesState && left.page == right.page &&
               left.editor.selectedStep == right.editor.selectedStep &&
               left.editor.selectedNote == right.editor.selectedNote &&
               left.editor.selectedVelocity == right.editor.selectedVelocity &&
               left.editor.selectedGate == right.editor.selectedGate &&
               left.editor.transportRunning == right.editor.transportRunning &&
               left.editor.shiftActive == right.editor.shiftActive &&
               left.midiClock.modalOpen == right.midiClock.modalOpen &&
               left.midiClock.active == right.midiClock.active &&
               left.midiClock.selection == right.midiClock.selection &&
               left.main.externalClockStatus == right.main.externalClockStatus &&
               left.main.externalTempo == right.main.externalTempo &&
               left.storage.state == right.storage.state &&
               left.storage.selection == right.storage.selection &&
               left.storage.action == right.storage.action &&
               left.storage.slot == right.storage.slot &&
               left.storage.status == right.storage.status &&
               left.storage.resetChoice == right.storage.resetChoice;
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
