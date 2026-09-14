#pragma once

#include <cstdint>

#include "transport_tick.h"

namespace SwingMetro {

using InternalTickRecord = TransportTickRecord;

template <std::size_t Capacity = 16>
using InternalTickStore = TransportTickStore<Capacity>;

class InternalTickSource {
  public:
    static constexpr std::uint8_t MIN_BPM = 40;
    static constexpr std::uint8_t MAX_BPM = 240;
    static constexpr std::uint32_t MICROSECONDS_PER_MINUTE = 60'000'000;
    static constexpr std::uint8_t PPQN = 24;

    [[nodiscard]] static constexpr auto clampBpm(std::uint8_t bpm) noexcept -> std::uint8_t {
        return bpm < MIN_BPM ? MIN_BPM : (bpm > MAX_BPM ? MAX_BPM : bpm);
    }

    [[nodiscard]] static constexpr auto periodForBpm(std::uint8_t bpm) noexcept -> std::uint32_t {
        return MICROSECONDS_PER_MINUTE / (static_cast<std::uint32_t>(clampBpm(bpm)) * PPQN);
    }

    auto start(std::uint32_t timestampUs, std::uint8_t bpm) noexcept -> std::uint32_t {
        _active.store(true, std::memory_order_release);
        setBpm(bpm);
        (void)_ticks.discard();
        const auto periodUs = nextPeriodUs();
        (void)_ticks.publish({timestampUs, periodUs});
        return periodUs;
    }

    auto stop() noexcept -> void {
        _active.store(false, std::memory_order_release);
        (void)_ticks.discard();
    }

    auto setBpm(std::uint8_t bpm) noexcept -> void {
        _bpm.store(clampBpm(bpm), std::memory_order_release);
        _fractionalUs = 0;
    }

    [[nodiscard]] auto onAlarm(std::uint32_t timestampUs) noexcept -> std::uint32_t {
        if (!_active.load(std::memory_order_acquire)) {
            return 0;
        }
        const auto periodUs = nextPeriodUs();
        (void)_ticks.publish({timestampUs, periodUs});
        return periodUs;
    }

    [[nodiscard]] auto ticks() noexcept -> InternalTickStore<>& { return _ticks; }

  private:
    [[nodiscard]] auto nextPeriodUs() noexcept -> std::uint32_t {
        const auto denominator =
            static_cast<std::uint32_t>(_bpm.load(std::memory_order_acquire)) * PPQN;
        const auto remainder = MICROSECONDS_PER_MINUTE % denominator;
        const auto periodUs =
            MICROSECONDS_PER_MINUTE / denominator + (_fractionalUs + remainder >= denominator);
        _fractionalUs = (_fractionalUs + remainder) % denominator;
        return periodUs;
    }

    InternalTickStore<> _ticks;
    std::atomic<bool> _active{false};
    std::atomic<std::uint8_t> _bpm{MIN_BPM};
    std::uint32_t _fractionalUs = 0;
};

} // namespace SwingMetro
