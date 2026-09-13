#pragma once

#include "midi_usb_packet.h"
#include "transport.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

struct MidiEvent {
    TransportPosition target{};
    MidiUsbPacket packet{};
    std::size_t sequenceNumber = 0;
};

enum class MidiEventQueueEnqueueResult : std::uint8_t {
    Ok,
    CapacityExceeded,
    TickQuotaExceeded,
};

struct MidiEventQueueDrainResult {
    std::array<MidiEvent, 16> events{};
    std::size_t count = 0;
    std::size_t lateCount = 0;
};

class MidiEventQueue {
  public:
    static constexpr std::size_t kCapacity = 16;
    static constexpr std::size_t kMaxPacketsPerTick = 8;

    [[nodiscard]] auto enqueue(TransportPosition target, const MidiUsbPacket& packet) noexcept
        -> MidiEventQueueEnqueueResult {
        if (_count == kCapacity) {
            return MidiEventQueueEnqueueResult::CapacityExceeded;
        }
        if (packetsAtTick(target.tick) == kMaxPacketsPerTick) {
            return MidiEventQueueEnqueueResult::TickQuotaExceeded;
        }

        const MidiEvent event{target, packet, _nextSequenceNumber++};
        std::size_t insertAt = _count;
        while (insertAt > 0 && eventPrecedes(event, _events[insertAt - 1])) {
            _events[insertAt] = _events[insertAt - 1];
            --insertAt;
        }
        _events[insertAt] = event;
        ++_count;
        return MidiEventQueueEnqueueResult::Ok;
    }

    [[nodiscard]] auto drainAt(TransportPosition position) noexcept -> MidiEventQueueDrainResult {
        MidiEventQueueDrainResult result;
        while (result.count < _count && _events[result.count].target <= position) {
            result.events[result.count] = _events[result.count];
            result.lateCount += _events[result.count].target < position;
            ++result.count;
        }

        for (std::size_t index = result.count; index < _count; ++index) {
            _events[index - result.count] = _events[index];
        }
        _count -= result.count;
        return result;
    }

    auto clear() noexcept -> void { _count = 0; }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return _count; }

    [[nodiscard]] auto empty() const noexcept -> bool { return _count == 0; }

  private:
    [[nodiscard]] auto packetsAtTick(TransportTick tick) const noexcept -> std::size_t {
        std::size_t count = 0;
        for (std::size_t index = 0; index < _count; ++index) {
            count += _events[index].target.tick == tick;
        }
        return count;
    }

    [[nodiscard]] static auto priority(const MidiEvent& event) noexcept -> std::uint8_t {
        const auto status = event.packet[1];
        if (status == 0xF8) {
            return 0;
        }
        if ((status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && event.packet[3] == 0)) {
            return 1;
        }
        if ((status & 0xF0) == 0x90) {
            return 2;
        }
        return 3;
    }

    [[nodiscard]] static auto eventPrecedes(const MidiEvent& left, const MidiEvent& right) noexcept
        -> bool {
        if (left.target != right.target) {
            return left.target < right.target;
        }
        if (left.target.phase != 0) {
            return left.sequenceNumber < right.sequenceNumber;
        }
        const auto leftPriority = priority(left);
        const auto rightPriority = priority(right);
        return leftPriority != rightPriority ? leftPriority < rightPriority
                                             : left.sequenceNumber < right.sequenceNumber;
    }

    std::array<MidiEvent, kCapacity> _events{};
    std::size_t _count = 0;
    std::size_t _nextSequenceNumber = 0;
};

} // namespace SwingMetro
