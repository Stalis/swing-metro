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

enum class InvalidationReason : std::uint8_t {
    Stop,
    ModeSwitch,
    Storage,
    ExternalClockLost,
    RetryWindowExceeded,
    DeliveryCapacity,
    Disconnected,
    SupersededStart,
};

enum class DeliveryRemovalReason : std::uint8_t {
    ClockCoalesced,
    ClockExpired,
    NoteOnExpired,
    Stop,
    ModeSwitch,
    Storage,
    ExternalClockLost,
    RetryWindowExceeded,
    DeliveryCapacity,
    Disconnected,
    SupersededStart,
    ScheduledOverdue,
    StaleGateOff,
    Count,
};

static constexpr std::size_t DELIVERY_REMOVAL_REASON_COUNT =
    static_cast<std::size_t>(DeliveryRemovalReason::Count);

[[nodiscard]] constexpr auto deliveryRemovalReason(InvalidationReason reason) noexcept
    -> DeliveryRemovalReason {
    switch (reason) {
    case InvalidationReason::Stop:
        return DeliveryRemovalReason::Stop;
    case InvalidationReason::ModeSwitch:
        return DeliveryRemovalReason::ModeSwitch;
    case InvalidationReason::Storage:
        return DeliveryRemovalReason::Storage;
    case InvalidationReason::ExternalClockLost:
        return DeliveryRemovalReason::ExternalClockLost;
    case InvalidationReason::RetryWindowExceeded:
        return DeliveryRemovalReason::RetryWindowExceeded;
    case InvalidationReason::DeliveryCapacity:
        return DeliveryRemovalReason::DeliveryCapacity;
    case InvalidationReason::Disconnected:
        return DeliveryRemovalReason::Disconnected;
    case InvalidationReason::SupersededStart:
        return DeliveryRemovalReason::SupersededStart;
    }
    return DeliveryRemovalReason::Stop;
}

struct PendingRemovalSummary {
    MidiMessageClassSummary byClass{};
    std::size_t noteOns = 0;
    std::size_t noteOffs = 0;
    std::size_t stops = 0;
    std::size_t terminalNoteOffs = 0;
    std::size_t terminalStops = 0;
};

struct PendingMidiEvent {
    MidiEvent event{};
    std::uint32_t deadlineUs = 0;
    std::size_t deliverySequenceNumber = 0;
    std::uint32_t sessionGeneration = 1;
    bool countsAsInternalClockAttempt = false;
    bool terminal = false;
    MidiAttemptLateness lateness = MidiAttemptLateness::None;
    std::uint8_t attemptOrdinal = 0;
    bool retrySeen = false;
};

class MidiPendingDeliveryQueue {
  public:
    static constexpr std::size_t CAPACITY = MidiEventQueue::CAPACITY;
    static constexpr std::size_t TERMINAL_RESERVE = 2;
    static constexpr std::size_t NORMAL_CAPACITY = CAPACITY - TERMINAL_RESERVE;

    [[nodiscard]] auto push(const MidiEvent& event, std::uint32_t deadlineUs,
                            std::size_t deliverySequenceNumber,
                            bool countsAsInternalClockAttempt = false,
                            MidiAttemptLateness lateness = MidiAttemptLateness::None,
                            std::uint32_t sessionGeneration = 1, bool terminal = false) noexcept
        -> bool {
        if (_count == CAPACITY) {
            return false;
        }
        _events[_tail] = {event,
                          deadlineUs,
                          deliverySequenceNumber,
                          sessionGeneration,
                          countsAsInternalClockAttempt,
                          terminal,
                          lateness,
                          0,
                          false};
        _tail = (_tail + 1) % CAPACITY;
        ++_count;
        return true;
    }

    [[nodiscard]] auto pushNormal(const MidiEvent& event, std::uint32_t deadlineUs,
                                  std::size_t deliverySequenceNumber,
                                  bool countsAsInternalClockAttempt = false,
                                  MidiAttemptLateness lateness = MidiAttemptLateness::None,
                                  std::uint32_t sessionGeneration = 1) noexcept -> bool {
        return _count < NORMAL_CAPACITY &&
               push(event, deadlineUs, deliverySequenceNumber, countsAsInternalClockAttempt,
                    lateness, sessionGeneration);
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

    auto markFrontRetry() noexcept -> void {
        if (_count == 0) {
            return;
        }
        auto& pending = _events[_head];
        if (pending.attemptOrdinal != UINT8_MAX) {
            ++pending.attemptOrdinal;
        }
        pending.retrySeen = true;
    }

    auto clear() noexcept -> void {
        _head = 0;
        _tail = 0;
        _count = 0;
    }

    template <typename Predicate>
    auto removeIf(Predicate predicate) noexcept -> PendingRemovalSummary {
        PendingRemovalSummary summary;
        std::array<PendingMidiEvent, CAPACITY> retained{};
        std::size_t retainedCount = 0;
        for (std::size_t index = 0; index < _count; ++index) {
            const auto& pending = _events[(_head + index) % CAPACITY];
            if (!predicate(pending)) {
                retained[retainedCount++] = pending;
                continue;
            }
            ++summary.byClass
                  .counts[static_cast<std::size_t>(pending.event.message.messageClass())];
            if (pending.event.message.isNoteOn()) {
                ++summary.noteOns;
            } else if (pending.event.message.isNoteOffEquivalent()) {
                ++summary.noteOffs;
                summary.terminalNoteOffs += pending.terminal;
            } else if (pending.event.message.type() == MidiMessageType::Stop) {
                ++summary.stops;
                summary.terminalStops += pending.terminal;
            }
        }
        _events = retained;
        _head = 0;
        _tail = retainedCount % CAPACITY;
        _count = retainedCount;
        return summary;
    }

    [[nodiscard]] auto hasTerminal(MidiMessageType type, std::uint32_t generation) const noexcept
        -> bool {
        for (std::size_t index = 0; index < _count; ++index) {
            const auto& pending = _events[(_head + index) % CAPACITY];
            if (pending.terminal && pending.sessionGeneration == generation &&
                pending.event.message.type() == type) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto hasTerminalNoteOff() const noexcept -> bool {
        for (std::size_t index = 0; index < _count; ++index) {
            const auto& pending = _events[(_head + index) % CAPACITY];
            if (pending.terminal && pending.event.message.isNoteOffEquivalent()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto hasNoteOff(std::uint32_t generation, MidiLaunchId launchId) const noexcept
        -> bool {
        for (std::size_t index = 0; index < _count; ++index) {
            const auto& pending = _events[(_head + index) % CAPACITY];
            if (pending.sessionGeneration == generation && pending.event.launchId == launchId &&
                pending.event.message.isNoteOffEquivalent()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto full() const noexcept -> bool { return _count == CAPACITY; }
    [[nodiscard]] auto empty() const noexcept -> bool { return _count == 0; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return _count; }
    [[nodiscard]] auto classSummary() const noexcept -> MidiMessageClassSummary {
        MidiMessageClassSummary summary;
        for (std::size_t index = 0; index < _count; ++index) {
            ++summary.counts[static_cast<std::size_t>(
                _events[(_head + index) % CAPACITY].event.message.messageClass())];
        }
        return summary;
    }

  private:
    std::array<PendingMidiEvent, CAPACITY> _events{};
    std::size_t _head = 0;
    std::size_t _tail = 0;
    std::size_t _count = 0;
};

} // namespace SwingMetro
