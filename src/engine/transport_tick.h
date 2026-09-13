#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

struct TransportTickRecord {
    std::uint32_t timestampUs = 0;
    std::uint32_t periodUs = 0;
};

template <std::size_t Capacity = 16>
class TransportTickStore {
  public:
    static_assert(Capacity > 1 && Capacity <= UINT8_MAX);

    [[nodiscard]] auto publish(TransportTickRecord record) noexcept -> bool {
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

    [[nodiscard]] auto pop(TransportTickRecord& record) noexcept -> bool {
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
        TransportTickRecord record;
        while (pop(record)) {
            ++count;
        }
        return count;
    }

    [[nodiscard]] auto overflowCount() const noexcept -> std::uint32_t {
        return _overflowCount.load(std::memory_order_relaxed);
    }

  private:
    std::array<TransportTickRecord, Capacity> _records{};
    std::atomic<std::uint8_t> _read{0};
    std::atomic<std::uint8_t> _write{0};
    std::atomic<std::uint32_t> _overflowCount{0};
};

} // namespace SwingMetro
