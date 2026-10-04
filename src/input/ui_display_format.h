#pragma once

#include "program/program_storage_modal.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace SwingMetro::UiDisplayFormat {

inline void midiChannels(std::uint16_t mask, char* buffer, std::size_t size) {
    if (mask == 0xFFFF) {
        std::snprintf(buffer, size, "ALL");
        return;
    }
    if (mask == 0) {
        std::snprintf(buffer, size, "OFF");
        return;
    }
    unsigned count = 0;
    unsigned channel = 0;
    for (unsigned index = 0; index < 16; ++index) {
        if ((mask & (1U << index)) != 0) {
            ++count;
            channel = index + 1;
        }
    }
    if (count == 1) {
        std::snprintf(buffer, size, "%u", channel);
    } else {
        std::snprintf(buffer, size, "%u/16", count);
    }
}

[[nodiscard]] constexpr int slotWindowStart(std::uint8_t slot) {
    const int selected = slot == PROGRAM_STORAGE_CANCEL_SLOT ? -1 : static_cast<int>(slot);
    const int start = selected - 2;
    return start < -1 ? -1 : start > 10 ? 10 : start;
}

} // namespace SwingMetro::UiDisplayFormat
