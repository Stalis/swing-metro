#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace UiNoteName {

inline void format(std::uint8_t midiNote, char* buffer, std::size_t size) {
    static constexpr const char* names[] = {"C",  "C#", "D",  "Eb", "E",  "F",
                                            "F#", "G",  "G#", "A",  "Bb", "B"};
    const auto relative = static_cast<unsigned>(midiNote) - 36U;
    std::snprintf(buffer, size, "%s%u", names[relative % 12U], relative / 12U);
}

} // namespace UiNoteName
