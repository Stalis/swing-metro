#pragma once

#include <array>
#include <cstdint>

namespace SwingMetro {

struct PadButtonIds {
    static constexpr std::array<std::uint8_t, 16> values = {
        0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15,
    };
};

} // namespace SwingMetro
