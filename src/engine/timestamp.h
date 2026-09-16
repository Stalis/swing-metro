#pragma once

#include <cstdint>

namespace SwingMetro {

// Timestamps use modulo-2^32 microseconds. Ordering is valid only when their real
// distance is strictly below 2^31 us (about 35.8 minutes). The antipodal distance is
// undefined.
constexpr std::uint32_t TIMESTAMP_COMPARISON_HORIZON_US = std::uint32_t{1} << 31U;

[[nodiscard]] constexpr auto timestampReached(std::uint32_t nowUs,
                                              std::uint32_t timestampUs) noexcept -> bool {
    return nowUs - timestampUs < TIMESTAMP_COMPARISON_HORIZON_US;
}

} // namespace SwingMetro
