#pragma once

#include <cstdint>

#include "transport_tick.h"

namespace SwingMetro {

using InternalTickRecord = TransportTickRecord;

template <std::size_t Capacity = 16>
using InternalTickStore = TransportTickStore<Capacity>;

class InternalTickSource {
  public:
    static constexpr std::uint8_t kMinBpm = 40;
    static constexpr std::uint8_t kMaxBpm = 240;
    static constexpr std::uint32_t kMicrosecondsPerMinute = 60'000'000;
    static constexpr std::uint8_t kPpqn = 24;

    [[nodiscard]] static constexpr auto clampBpm(std::uint8_t bpm) noexcept -> std::uint8_t {
        return bpm < kMinBpm ? kMinBpm : (bpm > kMaxBpm ? kMaxBpm : bpm);
    }

    [[nodiscard]] static constexpr auto periodForBpm(std::uint8_t bpm) noexcept -> std::uint32_t {
        return kMicrosecondsPerMinute / (static_cast<std::uint32_t>(clampBpm(bpm)) * kPpqn);
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
            static_cast<std::uint32_t>(_bpm.load(std::memory_order_acquire)) * kPpqn;
        const auto remainder = kMicrosecondsPerMinute % denominator;
        const auto periodUs =
            kMicrosecondsPerMinute / denominator + (_fractionalUs + remainder >= denominator);
        _fractionalUs = (_fractionalUs + remainder) % denominator;
        return periodUs;
    }

    InternalTickStore<> _ticks;
    std::atomic<bool> _active{false};
    std::atomic<std::uint8_t> _bpm{kMinBpm};
    std::uint32_t _fractionalUs = 0;
};

} // namespace SwingMetro
