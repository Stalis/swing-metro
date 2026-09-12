#pragma once

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
    bool transportRunning = true;
    bool shiftActive = false;
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
                                          static_cast<uint32_t>(settings.shiftActive) << 18;

        _generation.fetch_add(1, std::memory_order_seq_cst);
        _packed.store(packed, std::memory_order_seq_cst);
        _notesPacked.store(notesPacked, std::memory_order_seq_cst);
        _navigationPacked.store(navigationPacked, std::memory_order_seq_cst);
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
                .transportRunning = ((navigationPacked >> 17) & 1U) != 0,
                .shiftActive = ((navigationPacked >> 18) & 1U) != 0,
            };
        }
    }

  private:
    [[nodiscard]] static bool sameSettings(const UiSettings& left, const UiSettings& right) {
        return left.tempo == right.tempo && left.swing == right.swing &&
               left.volume == right.volume && left.activeNote == right.activeNote &&
               left.notesState == right.notesState && left.page == right.page &&
               left.selectedStep == right.selectedStep && left.selectedNote == right.selectedNote &&
               left.transportRunning == right.transportRunning &&
               left.shiftActive == right.shiftActive;
    }

    std::atomic<uint32_t> _packed{0};
    std::atomic<uint16_t> _notesPacked{0};
    std::atomic<uint32_t> _navigationPacked{0};
    std::atomic<uint32_t> _generation{0};
    std::optional<UiSettings> _lastPublished;
};
