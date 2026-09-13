#pragma once

#include "internal_tick_source.h"
#include "midi_event_queue.h"

#include <cstdint>

namespace SwingMetro {

struct InternalTickDiagnostics {
    std::uint32_t lateTicks = 0;
    std::uint32_t droppedTicks = 0;
    std::uint32_t lateEvents = 0;
};

class InternalTickConsumer {
  public:
    static constexpr std::uint8_t kMaxTicksPerPass = 4;

    InternalTickConsumer(Transport& transport, MidiEventQueue& queue) noexcept
        : _transport{transport}, _queue{queue} {}

    template <typename SendPacket>
    auto start(SendPacket sendPacket) -> void {
        _transport.start();
        _queue.clear();
        _open = false;
        _hasDeadline = false;
        sendPacket(usbMidiRealTimePacket(0xFA));
    }

    template <typename SendPacket>
    auto continuePlayback(SendPacket sendPacket) -> void {
        _transport.continuePlayback();
        sendPacket(usbMidiRealTimePacket(0xFB));
    }

    template <typename SendPacket>
    auto stop(SendPacket sendPacket) -> void {
        _transport.stop();
        _open = false;
        _hasDeadline = false;
        _queue.clear();
        sendPacket(usbMidiRealTimePacket(0xFC));
    }

    auto setBpm(std::uint8_t bpm) noexcept -> void {
        if (_open) {
            _periodUs = InternalTickSource::periodForBpm(bpm);
            setDeadlineForNextEvent();
        }
    }

    template <typename SendPacket>
    auto consume(InternalTickStore<>& ticks, std::uint32_t nowUs, SendPacket sendPacket) -> void {
        InternalTickRecord record;
        std::uint8_t processed = 0;
        while (processed < kMaxTicksPerPass && ticks.pop(record)) {
            _diagnostics.lateTicks += isDue(nowUs, record.timestampUs);
            openTick(record, sendPacket);
            ++processed;
        }
        _diagnostics.droppedTicks += ticks.discard();
        dispatchDue(nowUs, sendPacket);
    }

    template <typename SendPacket>
    auto dispatchDue(std::uint32_t nowUs, SendPacket sendPacket) -> void {
        while (_open && _hasDeadline && isDue(nowUs, _nextDeadlineUs)) {
            const auto next = _queue.nextPosition();
            if (!next.has_value() || next->tick != _transport.position().tick) {
                _hasDeadline = false;
                return;
            }
            sendAt(*next, sendPacket);
        }
    }

    [[nodiscard]] auto nextDeadlineUs() const noexcept -> std::uint32_t { return _nextDeadlineUs; }
    [[nodiscard]] auto diagnostics() const noexcept -> InternalTickDiagnostics {
        return _diagnostics;
    }

  private:
    [[nodiscard]] static auto isDue(std::uint32_t nowUs, std::uint32_t deadlineUs) noexcept
        -> bool {
        return static_cast<std::int32_t>(nowUs - deadlineUs) >= 0;
    }

    template <typename SendPacket>
    auto openTick(const InternalTickRecord& record, SendPacket sendPacket) -> void {
        if (!_transport.snapshot().running) {
            return;
        }
        if (_open) {
            (void)_transport.advanceTick();
        }
        _open = true;
        _tickStartUs = record.timestampUs;
        _periodUs = record.periodUs;
        (void)_queue.enqueue(_transport.position(), usbMidiRealTimePacket(0xF8));
        sendAt(_transport.position(), sendPacket);
    }

    template <typename SendPacket>
    auto sendAt(TransportPosition position, SendPacket sendPacket) -> void {
        const auto drained = _queue.drainAt(position);
        _diagnostics.lateEvents += drained.lateCount;
        for (std::size_t index = 0; index < drained.count; ++index) {
            sendPacket(drained.events[index].packet);
        }
        setDeadlineForNextEvent();
    }

    auto setDeadlineForNextEvent() noexcept -> void {
        const auto next = _queue.nextPosition();
        if (!next.has_value() || !_open || next->tick != _transport.position().tick) {
            _hasDeadline = false;
            return;
        }
        _nextDeadlineUs =
            _tickStartUs + static_cast<std::uint32_t>(
                               (static_cast<std::uint64_t>(_periodUs) * next->phase) / 65536U);
        _hasDeadline = true;
    }

    Transport& _transport;
    MidiEventQueue& _queue;
    InternalTickDiagnostics _diagnostics{};
    std::uint32_t _tickStartUs = 0;
    std::uint32_t _periodUs = 0;
    std::uint32_t _nextDeadlineUs = 0;
    bool _hasDeadline = false;
    bool _open = false;
};

} // namespace SwingMetro
