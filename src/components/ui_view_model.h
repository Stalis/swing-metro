#pragma once

#include <atomic>
#include <cstdint>

struct UiSettings {
    uint8_t tempo;
    uint8_t swing;
    uint8_t volume;
};

class UiViewModel {
public:
    // A single packed atomic keeps the GUI snapshot internally consistent.
    void publish(UiSettings settings) {
        const uint32_t packed = static_cast<uint32_t>(settings.tempo) |
                                static_cast<uint32_t>(settings.swing) << 8 |
                                static_cast<uint32_t>(settings.volume) << 16;
        _packed.store(packed, std::memory_order_release);
    }

    [[nodiscard]] UiSettings read() const {
        const uint32_t packed = _packed.load(std::memory_order_acquire);
        return {
            .tempo = static_cast<uint8_t>(packed),
            .swing = static_cast<uint8_t>(packed >> 8),
            .volume = static_cast<uint8_t>(packed >> 16),
        };
    }

private:
    std::atomic<uint32_t> _packed{0};
};
