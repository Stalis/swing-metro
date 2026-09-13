#pragma once

#include <cstddef>
#include <cstdint>

namespace SwingMetro {

[[nodiscard]] constexpr auto isErasedFlash(const std::uint8_t* bytes, std::size_t size) -> bool {
    for (std::size_t index = 0; index < size; ++index) {
        if (bytes[index] != UINT8_MAX) {
            return false;
        }
    }
    return true;
}

} // namespace SwingMetro
