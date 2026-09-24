#pragma once

#include <cstdint>
#include <limits>

namespace SwingMetro {

using TransportTick = std::uint64_t;
using TransportPhase = std::uint16_t;

constexpr TransportTick TICKS_PER_QUARTER = 24;
constexpr TransportTick TICKS_PER_SIXTEENTH = 6;
constexpr TransportPhase PHASE_MAX = UINT16_MAX;
constexpr std::uint64_t PHASE_COUNT = static_cast<std::uint64_t>(PHASE_MAX) + 1U;
constexpr std::uint8_t SWING_MIN_VALUE = 50;
constexpr std::uint8_t SWING_MAX_VALUE = 90;

[[nodiscard]] constexpr auto phaseOffsetUs(TransportPhase phase, std::uint32_t periodUs) noexcept
    -> std::uint32_t {
    constexpr auto phaseCount = static_cast<std::uint32_t>(PHASE_MAX) + 1U;
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(phase) * periodUs + phaseCount - 1U) / phaseCount);
}

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
        return PHASE_MAX;
    }
    return static_cast<TransportPhase>(static_cast<std::uint32_t>(percent) *
                                       (static_cast<std::uint32_t>(PHASE_MAX) + 1U) / 100U);
}

[[nodiscard]] constexpr auto gateDurationUnits(std::uint8_t gate) noexcept -> std::uint64_t {
    return TICKS_PER_SIXTEENTH * PHASE_COUNT * gate / 100U;
}

[[nodiscard]] constexpr auto addPhaseUnits(TransportPosition position, std::uint64_t units) noexcept
    -> TransportPosition {
    const auto phaseUnits = units % PHASE_COUNT;
    const auto phaseTotal = static_cast<std::uint64_t>(position.phase) + phaseUnits;
    const auto tickDelta = units / PHASE_COUNT + phaseTotal / PHASE_COUNT;
    if (tickDelta > std::numeric_limits<TransportTick>::max() - position.tick) {
        return {std::numeric_limits<TransportTick>::max(), PHASE_MAX};
    }
    return {position.tick + tickDelta, static_cast<TransportPhase>(phaseTotal % PHASE_COUNT)};
}

[[nodiscard]] constexpr auto gateDeadline(TransportPosition onPosition, std::uint8_t gate) noexcept
    -> TransportPosition {
    return addPhaseUnits(onPosition, gateDurationUnits(gate));
}

[[nodiscard]] constexpr auto clampSwingValue(std::uint8_t swing) noexcept -> std::uint8_t {
    if (swing < SWING_MIN_VALUE) {
        return SWING_MIN_VALUE;
    }
    return swing > SWING_MAX_VALUE ? SWING_MAX_VALUE : swing;
}

[[nodiscard]] constexpr auto swingPhase(std::uint8_t sixteenthStep, std::uint8_t swing) noexcept
    -> TransportPhase {
    const auto clampedSwing = clampSwingValue(swing);
    if ((sixteenthStep % 2U) == 0U || clampedSwing == SWING_MIN_VALUE) {
        return 0;
    }
    return phaseFromPercent(clampedSwing);
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
                static_cast<std::uint8_t>(_position.tick % TICKS_PER_SIXTEENTH)};
    }

    [[nodiscard]] constexpr auto position() const noexcept -> TransportPosition {
        return _position;
    }

  private:
    TransportPosition _position{};
    bool _running = false;
};

} // namespace SwingMetro
