#pragma once

#include <cstdint>
#include <limits>

namespace SwingMetro {

using TransportTick = std::uint64_t;
using TransportPhase = std::uint16_t;

constexpr TransportTick kTicksPerQuarter = 24;
constexpr TransportTick kTicksPerSixteenth = 6;
constexpr TransportPhase kPhaseMax = UINT16_MAX;

struct TransportPosition {
    TransportTick tick = 0;
    TransportPhase phase = 0;

    [[nodiscard]] constexpr auto operator==(const TransportPosition& other) const noexcept -> bool {
        return tick == other.tick && phase == other.phase;
    }

    [[nodiscard]] constexpr auto operator!=(const TransportPosition& other) const noexcept -> bool {
        return !(*this == other);
    }

    [[nodiscard]] constexpr auto operator<(const TransportPosition& other) const noexcept -> bool {
        return tick < other.tick || (tick == other.tick && phase < other.phase);
    }

    [[nodiscard]] constexpr auto operator<=(const TransportPosition& other) const noexcept -> bool {
        return !(other < *this);
    }

    [[nodiscard]] constexpr auto operator>(const TransportPosition& other) const noexcept -> bool {
        return other < *this;
    }

    [[nodiscard]] constexpr auto operator>=(const TransportPosition& other) const noexcept -> bool {
        return !(*this < other);
    }
};

[[nodiscard]] constexpr auto phaseFromPercent(std::uint8_t percent) noexcept -> TransportPhase {
    if (percent >= 100) {
        return kPhaseMax;
    }
    return static_cast<TransportPhase>(static_cast<std::uint32_t>(percent) *
                                       (static_cast<std::uint32_t>(kPhaseMax) + 1U) / 100U);
}

struct TransportSnapshot {
    bool running = false;
    TransportTick tick = 0;
    std::uint8_t tickInSixteenth = 0;
};

class Transport {
  public:
    constexpr Transport() = default;
    constexpr explicit Transport(TransportTick tick) noexcept : _position{tick, 0} {}

    constexpr auto start() noexcept -> void {
        resetPosition();
        _running = true;
    }

    constexpr auto continuePlayback() noexcept -> void { _running = true; }

    constexpr auto stop() noexcept -> void { _running = false; }

    constexpr auto resetPosition() noexcept -> void { _position = {}; }

    [[nodiscard]] constexpr auto advanceTick() noexcept -> bool {
        if (!_running || _position.tick == std::numeric_limits<TransportTick>::max()) {
            return false;
        }
        ++_position.tick;
        return true;
    }

    [[nodiscard]] constexpr auto snapshot() const noexcept -> TransportSnapshot {
        return {_running, _position.tick,
                static_cast<std::uint8_t>(_position.tick % kTicksPerSixteenth)};
    }

    [[nodiscard]] constexpr auto position() const noexcept -> TransportPosition {
        return _position;
    }

  private:
    TransportPosition _position{};
    bool _running = false;
};

} // namespace SwingMetro
