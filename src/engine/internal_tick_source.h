#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

struct InternalTickRecord {
    std::uint32_t timestampUs = 0;
    std::uint32_t periodUs = 0;
};

template <std::size_t Capacity = 16>
class InternalTickStore {
  public:
    static_assert(Capacity > 1 && Capacity <= UINT8_MAX);

    [[nodiscard]] auto publish(InternalTickRecord record) noexcept -> bool {
        const auto write = _write.load(std::memory_order_relaxed);
        const auto next = static_cast<std::uint8_t>((write + 1) % Capacity);
        if (next == _read.load(std::memory_order_acquire)) {
            ++_overflowCount;
            return false;
        }
        _records[write] = record;
        _write.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] auto pop(InternalTickRecord& record) noexcept -> bool {
        const auto read = _read.load(std::memory_order_relaxed);
        if (read == _write.load(std::memory_order_acquire)) {
            return false;
        }
        record = _records[read];
        _read.store(static_cast<std::uint8_t>((read + 1) % Capacity), std::memory_order_release);
        return true;
    }

    [[nodiscard]] auto discard() noexcept -> std::size_t {
        std::size_t count = 0;
        InternalTickRecord record;
        while (pop(record)) {
            ++count;
        }
        return count;
    }

    [[nodiscard]] auto overflowCount() const noexcept -> std::uint32_t {
        return _overflowCount.load(std::memory_order_relaxed);
    }

  private:
    std::array<InternalTickRecord, Capacity> _records{};
    std::atomic<std::uint8_t> _read{0};
    std::atomic<std::uint8_t> _write{0};
    std::atomic<std::uint32_t> _overflowCount{0};
};

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
