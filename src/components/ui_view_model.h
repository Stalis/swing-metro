#pragma once

#include <atomic>
#include <bitset>
#include <cstdint>

struct UiSettings {
    uint8_t tempo;
    uint8_t swing;
    uint8_t volume;
    uint8_t activeNote;

    std::bitset<16> notesState;
};

class UiViewModel {
  public:
    // A single packed atomic keeps the GUI snapshot internally consistent.
    void publish(UiSettings settings) {
        const uint32_t packed = static_cast<uint32_t>(settings.tempo) |
                                static_cast<uint32_t>(settings.swing) << 8 |
                                static_cast<uint32_t>(settings.volume) << 16 |
                                static_cast<uint32_t>(settings.activeNote) << (16 + 8);
        _packed.store(packed, std::memory_order_release);

        const uint16_t notesPacked = static_cast<uint16_t>(settings.notesState.to_ulong());
        _notesPacked.store(notesPacked, std::memory_order_release);
    }

    [[nodiscard]] UiSettings read() const {
        const uint32_t packed = _packed.load(std::memory_order_acquire);
        const uint32_t notesPacked = _notesPacked.load(std::memory_order_acquire);

        return {
            .tempo = static_cast<uint8_t>(packed),
            .swing = static_cast<uint8_t>(packed >> 8),
            .volume = static_cast<uint8_t>(packed >> 16),
            .activeNote = static_cast<uint8_t>(packed >> (16 + 8)),
            .notesState = std::bitset<16>(static_cast<uint16_t>(notesPacked)),
        };
    }

  private:
    std::atomic<uint32_t> _packed{0};
    std::atomic<uint16_t> _notesPacked{0};
};
