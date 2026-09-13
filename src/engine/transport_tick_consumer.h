#pragma once

#include "midi_event_queue.h"
#include "transport_tick.h"

#include <cstdint>

namespace SwingMetro {

enum class TransportTickSource : std::uint8_t { Internal, External };

struct TransportTickDiagnostics {
    std::uint32_t lateTicks = 0;
    std::uint32_t droppedTicks = 0;
    std::uint32_t lateEvents = 0;
};

class TransportTickConsumer {
  public:
    static constexpr std::uint8_t kMaxTicksPerPass = 4;

    TransportTickConsumer(Transport& transport, MidiEventQueue& queue,
                          TransportTickSource source = TransportTickSource::Internal) noexcept
        : _transport{transport}, _queue{queue}, _source{source} {}

    template <typename SendPacket>
    auto start(SendPacket sendPacket) -> void {
        _transport.start();
        _queue.clear();
        closeTick();
        sendCommand(0xFA, sendPacket);
    }

    template <typename SendPacket>
    auto continuePlayback(SendPacket sendPacket) -> void {
        _transport.continuePlayback();
        sendCommand(0xFB, sendPacket);
    }

    template <typename SendPacket>
    auto stop(SendPacket sendPacket) -> void {
        _transport.stop();
        _queue.clear();
        closeTick();
        sendCommand(0xFC, sendPacket);
    }

    auto setBpm(std::uint8_t bpm) noexcept -> void {
        if (_open) {
            const auto clampedBpm = bpm < 40 ? 40U : (bpm > 240 ? 240U : bpm);
            _periodUs = 60'000'000U / (static_cast<std::uint32_t>(clampedBpm) * 24U);
            setDeadlineForNextEvent();
        }
    }

    template <typename TickStore, typename SendPacket>
    auto consume(TickStore& ticks, std::uint32_t nowUs, SendPacket sendPacket) -> void {
        TransportTickRecord record;
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
    auto consumeTick(TransportTickRecord record, std::uint32_t nowUs, SendPacket sendPacket)
        -> void {
        _diagnostics.lateTicks += isDue(nowUs, record.timestampUs);
        openTick(record, sendPacket);
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
    [[nodiscard]] auto diagnostics() const noexcept -> TransportTickDiagnostics {
        return _diagnostics;
    }

  private:
    [[nodiscard]] static auto isDue(std::uint32_t nowUs, std::uint32_t deadlineUs) noexcept
        -> bool {
        return static_cast<std::int32_t>(nowUs - deadlineUs) >= 0;
    }

    template <typename SendPacket>
    auto sendCommand(std::uint8_t command, SendPacket sendPacket) -> void {
        if (_source == TransportTickSource::Internal) {
            sendPacket(usbMidiRealTimePacket(command));
        }
    }

    template <typename SendPacket>
    auto openTick(const TransportTickRecord& record, SendPacket sendPacket) -> void {
        if (!_transport.snapshot().running) {
            return;
        }
        if (_open) {
            if (_source == TransportTickSource::External) {
                _queue.discardAt(_transport.position().tick);
            }
            (void)_transport.advanceTick();
        }
        _open = true;
        _tickStartUs = record.timestampUs;
        _periodUs = record.periodUs;
        if (_source == TransportTickSource::Internal) {
            (void)_queue.enqueue(_transport.position(), usbMidiRealTimePacket(0xF8));
        }
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

    auto closeTick() noexcept -> void {
        _open = false;
        _hasDeadline = false;
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
    TransportTickDiagnostics _diagnostics{};
    std::uint32_t _tickStartUs = 0;
    std::uint32_t _periodUs = 0;
    std::uint32_t _nextDeadlineUs = 0;
    TransportTickSource _source;
    bool _hasDeadline = false;
    bool _open = false;
};

} // namespace SwingMetro
