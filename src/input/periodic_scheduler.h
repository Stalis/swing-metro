#pragma once

#include <cstdint>

namespace SwingMetro {

template <std::uint32_t PERIOD>
class PeriodicScheduler {
  public:
    static constexpr std::uint32_t HALF_RANGE = std::uint32_t{1} << 31U;
    static_assert(PERIOD > 0U && PERIOD < HALF_RANGE,
                  "period must be nonzero and below the uint32 half-range");

    static constexpr std::uint32_t period = PERIOD;

    auto start(std::uint32_t now) noexcept -> void {
        _nextDeadline = now + PERIOD;
        _started = true;
    }

    [[nodiscard]] auto poll(std::uint32_t now) noexcept -> bool {
        // Modular ordering is unambiguous while consecutive polls are less than HALF_RANGE apart.
        if (!_started || now - _nextDeadline >= HALF_RANGE) {
            return false;
        }

        const auto missedPeriods = (now - _nextDeadline) / PERIOD;
        _nextDeadline += (missedPeriods + 1U) * PERIOD;
        return true;
    }

    [[nodiscard]] auto nextDeadline() const noexcept -> std::uint32_t { return _nextDeadline; }

  private:
    std::uint32_t _nextDeadline = 0;
    bool _started = false;
};

} // namespace SwingMetro
