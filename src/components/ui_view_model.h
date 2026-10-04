#pragma once

#include "ui_snapshot.h"

#include <array>
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
        const uint32_t mainMetaPacked = static_cast<uint32_t>(settings.main.sequenceNumber) |
                                        static_cast<uint32_t>(settings.main.midiChannelMask) << 8 |
                                        static_cast<uint32_t>(settings.main.highlightedStep) << 24;
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
        _mainMetaPacked.store(mainMetaPacked, std::memory_order_seq_cst);
        _mainDisplayPacked.store(static_cast<uint32_t>(settings.main.sequenceLength) |
                                     static_cast<uint32_t>(settings.main.clockMode) << 8,
                                 std::memory_order_seq_cst);
        for (std::size_t index = 0; index < _stepPacked.size(); ++index) {
            _stepPacked[index].store(
                static_cast<uint32_t>(settings.main.stepNotes[index]) |
                    static_cast<uint32_t>(settings.main.stepVelocities[index]) << 8 |
                    static_cast<uint32_t>(settings.main.stepGates[index]) << 16,
                std::memory_order_seq_cst);
        }
        _navigationPacked.store(navigationPacked, std::memory_order_seq_cst);
        _selectedGate.store(settings.editor.selectedGate, std::memory_order_seq_cst);
        _editorMockPacked.store(static_cast<uint32_t>(settings.editor.mode) |
                                    static_cast<uint32_t>(settings.editor.repeatCount) << 8,
                                std::memory_order_seq_cst);
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
            const uint32_t mainMetaPacked = _mainMetaPacked.load(std::memory_order_seq_cst);
            const uint32_t mainDisplayPacked = _mainDisplayPacked.load(std::memory_order_seq_cst);
            std::array<uint8_t, 16> stepNotes{};
            std::array<uint8_t, 16> stepVelocities{};
            std::array<uint8_t, 16> stepGates{};
            for (std::size_t index = 0; index < _stepPacked.size(); ++index) {
                const uint32_t step = _stepPacked[index].load(std::memory_order_seq_cst);
                stepNotes[index] = static_cast<uint8_t>(step);
                stepVelocities[index] = static_cast<uint8_t>(step >> 8);
                stepGates[index] = static_cast<uint8_t>(step >> 16);
            }
            const uint32_t navigationPacked = _navigationPacked.load(std::memory_order_seq_cst);
            const uint8_t selectedGate = _selectedGate.load(std::memory_order_seq_cst);
            const uint32_t editorMockPacked = _editorMockPacked.load(std::memory_order_seq_cst);
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
                         .stepNotes = stepNotes,
                         .stepVelocities = stepVelocities,
                         .stepGates = stepGates,
                         .sequenceNumber = static_cast<uint8_t>(mainMetaPacked),
                         .sequenceLength = static_cast<uint8_t>(mainDisplayPacked),
                         .midiChannelMask = static_cast<uint16_t>(mainMetaPacked >> 8),
                         .highlightedStep = static_cast<uint8_t>(mainMetaPacked >> 24),
                         .clockMode =
                             static_cast<SwingMetro::MidiClockMode>(mainDisplayPacked >> 8),
                         .externalClockStatus = static_cast<SwingMetro::ExternalMidiClockStatus>(
                             (externalClockPacked >> 8) & 3U),
                         .externalTempo = static_cast<uint8_t>(externalClockPacked)},
                .editor = {.selectedStep = static_cast<uint8_t>(navigationPacked),
                           .selectedNote = static_cast<uint8_t>(navigationPacked >> 8),
                           .selectedVelocity = static_cast<uint8_t>(navigationPacked >> 19),
                           .selectedGate = selectedGate,
                           .mode = static_cast<UiStepMode>(editorMockPacked),
                           .repeatCount = static_cast<uint8_t>(editorMockPacked >> 8),
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
               left.main.stepNotes == right.main.stepNotes &&
               left.main.stepVelocities == right.main.stepVelocities &&
               left.main.stepGates == right.main.stepGates &&
               left.main.sequenceNumber == right.main.sequenceNumber &&
               left.main.sequenceLength == right.main.sequenceLength &&
               left.main.midiChannelMask == right.main.midiChannelMask &&
               left.main.highlightedStep == right.main.highlightedStep &&
               left.main.clockMode == right.main.clockMode &&
               left.editor.selectedStep == right.editor.selectedStep &&
               left.editor.selectedNote == right.editor.selectedNote &&
               left.editor.selectedVelocity == right.editor.selectedVelocity &&
               left.editor.selectedGate == right.editor.selectedGate &&
               left.editor.mode == right.editor.mode &&
               left.editor.repeatCount == right.editor.repeatCount &&
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
    std::atomic<uint32_t> _mainMetaPacked{0};
    std::atomic<uint32_t> _mainDisplayPacked{0};
    std::array<std::atomic<uint32_t>, 16> _stepPacked{};
    std::atomic<uint32_t> _navigationPacked{0};
    std::atomic<uint8_t> _selectedGate{100};
    std::atomic<uint32_t> _editorMockPacked{0};
    std::atomic<uint32_t> _externalClockPacked{0};
    std::atomic<uint32_t> _programStoragePacked{0};
    std::atomic<uint32_t> _generation{0};
    std::optional<UiSettings> _lastPublished;
};
