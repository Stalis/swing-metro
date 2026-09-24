#pragma once

#include "midi_message.h"
#include "transport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace SwingMetro {

using MidiLaunchId = std::uint32_t;

struct MidiEvent {
    TransportPosition target{};
    MidiMessage message{};
    std::size_t sequenceNumber = 0;
    MidiLaunchId launchId = 0;
    std::uint32_t sessionGeneration = 0;
    TransportPosition gateDeadline{};
};

struct MidiEventRequest {
    TransportPosition target{};
    MidiMessage message{};
    MidiLaunchId launchId = 0;
    std::uint32_t sessionGeneration = 0;
    TransportPosition gateDeadline{};
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

    [[nodiscard]] auto enqueue(TransportPosition target, const MidiMessage& message) noexcept
        -> MidiEventQueueEnqueueResult {
        const MidiEventRequest request{target, message};
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
                    nonClockCount += !requests[index].message.isClock();
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

    [[nodiscard]] auto front() const noexcept -> std::optional<MidiEvent> {
        if (_count == 0) {
            return std::nullopt;
        }
        return _events[0];
    }

    auto popFront() noexcept -> void {
        if (_count == 0) {
            return;
        }
        for (std::size_t index = 1; index < _count; ++index) {
            _events[index - 1] = _events[index];
        }
        --_count;
    }

    [[nodiscard]] auto clear() noexcept -> MidiMessageClassSummary {
        MidiMessageClassSummary summary;
        for (std::size_t index = 0; index < _count; ++index) {
            ++summary.counts[static_cast<std::size_t>(_events[index].message.messageClass())];
        }
        _count = 0;
        return summary;
    }

    [[nodiscard]] auto discardAt(TransportTick tick) noexcept -> MidiMessageClassSummary {
        MidiMessageClassSummary summary;
        std::size_t write = 0;
        for (std::size_t read = 0; read < _count; ++read) {
            if (_events[read].target.tick != tick) {
                _events[write++] = _events[read];
            } else {
                ++summary.counts[static_cast<std::size_t>(_events[read].message.messageClass())];
            }
        }
        _count = write;
        return summary;
    }

    template <typename Predicate>
    [[nodiscard]] auto removeIf(Predicate predicate) noexcept -> MidiMessageClassSummary {
        MidiMessageClassSummary summary;
        std::size_t write = 0;
        for (std::size_t read = 0; read < _count; ++read) {
            if (!predicate(_events[read])) {
                _events[write++] = _events[read];
                continue;
            }
            ++summary.counts[static_cast<std::size_t>(_events[read].message.messageClass())];
        }
        _count = write;
        return summary;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return _count; }

    [[nodiscard]] auto empty() const noexcept -> bool { return _count == 0; }

    [[nodiscard]] auto classSummary() const noexcept -> MidiMessageClassSummary {
        MidiMessageClassSummary summary;
        for (std::size_t index = 0; index < _count; ++index) {
            ++summary.counts[static_cast<std::size_t>(_events[index].message.messageClass())];
        }
        return summary;
    }

    [[nodiscard]] auto nextPosition() const noexcept -> std::optional<TransportPosition> {
        if (_count == 0) {
            return std::nullopt;
        }
        return _events[0].target;
    }

  private:
    auto enqueueUnchecked(const MidiEventRequest& request) noexcept -> void {
        const MidiEvent event{request.target,   request.message,           _nextSequenceNumber++,
                              request.launchId, request.sessionGeneration, request.gateDeadline};
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
            count += _events[index].target.tick == tick && !_events[index].message.isClock();
        }
        return count;
    }

    [[nodiscard]] static auto priority(const MidiEvent& event) noexcept -> std::uint8_t {
        return event.message.priority();
    }

    [[nodiscard]] static auto eventPrecedes(const MidiEvent& left, const MidiEvent& right) noexcept
        -> bool {
        if (left.target != right.target) {
            return left.target < right.target;
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
