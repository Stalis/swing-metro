#pragma once

#include "midi_event_queue.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace SwingMetro {

enum class MidiAttemptLateness : std::uint8_t {
    None,
    ClockAttempt,
    QueuedEventAttempt,
};

struct PendingMidiEvent {
    MidiEvent event{};
    std::uint32_t deadlineUs = 0;
    std::size_t deliverySequenceNumber = 0;
    bool countsAsInternalClockAttempt = false;
    MidiAttemptLateness lateness = MidiAttemptLateness::None;
};

class MidiPendingDeliveryQueue {
  public:
    static constexpr std::size_t CAPACITY = MidiEventQueue::CAPACITY;

    [[nodiscard]] auto push(const MidiEvent& event, std::uint32_t deadlineUs,
                            std::size_t deliverySequenceNumber,
                            bool countsAsInternalClockAttempt = false,
                            MidiAttemptLateness lateness = MidiAttemptLateness::None) noexcept
        -> bool {
        if (_count == CAPACITY) {
            return false;
        }
        _events[_tail] = {event, deadlineUs, deliverySequenceNumber, countsAsInternalClockAttempt,
                          lateness};
        _tail = (_tail + 1) % CAPACITY;
        ++_count;
        return true;
    }

    [[nodiscard]] auto front() const noexcept -> std::optional<PendingMidiEvent> {
        if (_count == 0) {
            return std::nullopt;
        }
        return _events[_head];
    }

    auto popFront() noexcept -> void {
        if (_count == 0) {
            return;
        }
        _head = (_head + 1) % CAPACITY;
        --_count;
    }

    auto clear() noexcept -> void {
        _head = 0;
        _tail = 0;
        _count = 0;
    }

    [[nodiscard]] auto full() const noexcept -> bool { return _count == CAPACITY; }
    [[nodiscard]] auto empty() const noexcept -> bool { return _count == 0; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return _count; }

  private:
    std::array<PendingMidiEvent, CAPACITY> _events{};
    std::size_t _head = 0;
    std::size_t _tail = 0;
    std::size_t _count = 0;
};

} // namespace SwingMetro
