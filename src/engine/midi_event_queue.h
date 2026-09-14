#pragma once

#include "midi_usb_packet.h"
#include "transport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace SwingMetro {

struct MidiEvent {
    TransportPosition target{};
    MidiUsbPacket packet{};
    std::size_t sequenceNumber = 0;
};

struct MidiEventRequest {
    TransportPosition target{};
    MidiUsbPacket packet{};
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
    static constexpr std::size_t CAPACITY = 16;
    static constexpr std::size_t MAX_PACKETS_PER_TICK = 8;

    [[nodiscard]] auto enqueue(TransportPosition target, const MidiUsbPacket& packet) noexcept
        -> MidiEventQueueEnqueueResult {
        const MidiEventRequest request{target, packet};
        return enqueueBatch(&request, 1);
    }

    [[nodiscard]] auto enqueueBatch(const MidiEventRequest* requests, std::size_t count) noexcept
        -> MidiEventQueueEnqueueResult {
        if (_count + count > CAPACITY) {
            return MidiEventQueueEnqueueResult::CapacityExceeded;
        }
        for (std::size_t requestIndex = 0; requestIndex < count; ++requestIndex) {
            const auto tick = requests[requestIndex].target.tick;
            std::size_t packetCount = packetsAtTick(tick);
            std::size_t nonClockCount = nonClockPacketsAtTick(tick);
            for (std::size_t index = 0; index < count; ++index) {
                if (requests[index].target.tick == tick) {
                    ++packetCount;
                    nonClockCount += requests[index].packet[1] != 0xF8;
                }
            }
            if (packetCount > MAX_PACKETS_PER_TICK || nonClockCount >= MAX_PACKETS_PER_TICK) {
                return MidiEventQueueEnqueueResult::TickQuotaExceeded;
            }
        }

        for (std::size_t index = 0; index < count; ++index) {
            enqueueUnchecked(requests[index]);
        }
        return MidiEventQueueEnqueueResult::Ok;
    }

    [[nodiscard]] auto enqueueBatch(const std::array<MidiEventRequest, 2>& requests,
                                    std::size_t count) noexcept -> MidiEventQueueEnqueueResult {
        return enqueueBatch(requests.data(), count);
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

    auto discardAt(TransportTick tick) noexcept -> void {
        std::size_t write = 0;
        for (std::size_t read = 0; read < _count; ++read) {
            if (_events[read].target.tick != tick) {
                _events[write++] = _events[read];
            }
        }
        _count = write;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return _count; }

    [[nodiscard]] auto empty() const noexcept -> bool { return _count == 0; }

    [[nodiscard]] auto nextPosition() const noexcept -> std::optional<TransportPosition> {
        if (_count == 0) {
            return std::nullopt;
        }
        return _events[0].target;
    }

  private:
    auto enqueueUnchecked(const MidiEventRequest& request) noexcept -> void {
        const MidiEvent event{request.target, request.packet, _nextSequenceNumber++};
        std::size_t insertAt = _count;
        while (insertAt > 0 && eventPrecedes(event, _events[insertAt - 1])) {
            _events[insertAt] = _events[insertAt - 1];
            --insertAt;
        }
        _events[insertAt] = event;
        ++_count;
    }

    [[nodiscard]] auto packetsAtTick(TransportTick tick) const noexcept -> std::size_t {
        std::size_t count = 0;
        for (std::size_t index = 0; index < _count; ++index) {
            count += _events[index].target.tick == tick;
        }
        return count;
    }

    [[nodiscard]] auto nonClockPacketsAtTick(TransportTick tick) const noexcept -> std::size_t {
        std::size_t count = 0;
        for (std::size_t index = 0; index < _count; ++index) {
            count += _events[index].target.tick == tick && _events[index].packet[1] != 0xF8;
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

    std::array<MidiEvent, CAPACITY> _events{};
    std::size_t _count = 0;
    std::size_t _nextSequenceNumber = 0;
};

} // namespace SwingMetro
