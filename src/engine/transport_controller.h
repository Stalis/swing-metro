#pragma once

#include "external_midi_clock.h"
#include "internal_tick_source.h"
#include "midi_clock_mode.h"
#include "midi_event_queue.h"
#include "midi_usb_packet.h"
#include "sequencer.h"
#include "transport.h"

#include <cstdint>
#include <optional>

namespace SwingMetro {

class MidiPacketSink {
  public:
    virtual ~MidiPacketSink() = default;
    virtual auto send(const MidiUsbPacket& packet) -> void = 0;
};

class MidiDispatcher {
  public:
    MidiDispatcher(MidiEventQueue& queue, Transport& transport, Sequencer& sequencer,
                   MidiPacketSink& sink) noexcept
        : _queue{queue}, _transport{transport}, _sequencer{sequencer}, _sink{sink} {}

    auto start(bool emitStart) -> void {
        _transport.start();
        _haveTick = false;
        _internalOutputActive = emitStart;
        if (emitStart) {
            _sink.send(usbMidiRealTimePacket(0xFA));
        }
    }

    auto continuePlayback() noexcept -> void {
        _transport.continuePlayback();
        _haveTick = false;
    }

    auto stop(std::optional<MIDI_Note> note) -> void {
        if (note.has_value()) {
            _sink.send({0x08, 0x80, *note, 0});
        }
        if (_internalOutputActive) {
            _sink.send(usbMidiRealTimePacket(0xFC));
        }
        _internalOutputActive = false;
        if (_haveTick) {
            (void)_transport.advanceTick();
        }
        _haveTick = false;
        _transport.stop();
    }

    auto dispatchDue(std::uint32_t nowUs) -> void {
        if (!_haveTick || !_transport.snapshot().running) {
            return;
        }
        const auto elapsed = nowUs - _tickStartUs;
        const auto phase =
            elapsed >= _tickPeriodUs
                ? kPhaseMax
                : static_cast<TransportPhase>((static_cast<std::uint64_t>(elapsed) *
                                               (static_cast<std::uint32_t>(kPhaseMax) + 1U)) /
                                              _tickPeriodUs);
        sendDue({_transport.position().tick, phase});
    }

    auto consumeTick(TransportTickRecord record, bool emitClock) -> void {
        if (!_transport.snapshot().running || record.periodUs == 0) {
            return;
        }
        if (_haveTick) {
            dispatchDue(record.timestampUs);
            (void)_transport.advanceTick();
        }
        _tickStartUs = record.timestampUs;
        _tickPeriodUs = record.periodUs;
        _haveTick = true;
        if (emitClock) {
            _sink.send(usbMidiRealTimePacket(0xF8));
        }
        sendDue({_transport.position().tick, 0});
        _sequencer.notifyBoundaryReached(_transport.position().tick);
    }

    [[nodiscard]] auto position() const noexcept -> TransportPosition {
        return _transport.position();
    }

  private:
    auto sendDue(TransportPosition position) -> void {
        const auto due = _queue.drainAt(position);
        for (std::size_t index = 0; index < due.count; ++index) {
            _sink.send(due.events[index].packet);
        }
    }

    MidiEventQueue& _queue;
    Transport& _transport;
    Sequencer& _sequencer;
    MidiPacketSink& _sink;
    std::uint32_t _tickStartUs = 0;
    std::uint32_t _tickPeriodUs = 0;
    bool _haveTick = false;
    bool _internalOutputActive = false;
};

class TransportController {
  public:
    TransportController(Sequencer& sequencer, MidiClockSettings& settings,
                        MidiPacketSink& sink) noexcept
        : _sequencer{sequencer}, _settings{settings},
          _dispatcher{_queue, _transport, sequencer, sink} {}

    auto toggle(std::uint32_t) -> void {
        if (_sequencer.isRunning()) {
            stop();
        } else {
            start(_settings.mode() == MidiClockMode::External);
        }
    }

    auto applyMode(MidiClockMode mode) -> void {
        if (mode == _settings.mode()) {
            return;
        }
        stop();
        _external.reset();
        _settings.apply(mode);
    }

    auto openStorage() -> void {
        _storageOpen = true;
        stop();
    }

    auto closeStorage() noexcept -> void { _storageOpen = false; }

    auto handleExternal(const MidiRealtimeEvent& event) -> void {
        if (_storageOpen || _settings.mode() != MidiClockMode::External) {
            return;
        }
        const auto result = _external.handle(event);
        if (result.reset) {
            start(true);
        } else if (result.started && !_sequencer.isRunning()) {
            _sequencer.continuePlayback();
            _transport.continuePlayback();
            _dispatcher.continuePlayback();
        }
        if (result.stopped) {
            stop(false);
        } else if (result.tick) {
            _dispatcher.consumeTick(result.tickRecord, false);
        }
    }

    auto process(std::uint32_t nowUs, InternalTickStore<>& ticks) -> void {
        if (_storageOpen) {
            (void)ticks.discard();
            return;
        }
        if (_internalTiming) {
            TransportTickRecord record;
            while (ticks.pop(record)) {
                _dispatcher.consumeTick(record, _settings.mode() == MidiClockMode::Internal);
            }
        } else {
            (void)ticks.discard();
        }
        _dispatcher.dispatchDue(nowUs);
        (void)_sequencer.scheduleThrough(_dispatcher.position(), _queue);
        if (_settings.mode() == MidiClockMode::External) {
            const auto result = _external.update(nowUs);
            if (result.stopped) {
                stop(false);
            }
        }
    }

    [[nodiscard]] auto usesInternalTiming() const noexcept -> bool { return _internalTiming; }
    [[nodiscard]] auto externalStatus() const noexcept -> ExternalMidiClockStatus {
        return _external.status();
    }
    [[nodiscard]] auto externalBpm() const noexcept -> std::uint8_t { return _external.bpm(); }

  private:
    auto start(bool waitForExternalTick) -> void {
        _queue.clear();
        const auto note = _sequencer.stop();
        _dispatcher.stop(note);
        _sequencer.start();
        _dispatcher.start(_settings.mode() == MidiClockMode::Internal);
        _internalTiming = !waitForExternalTick;
        (void)_sequencer.scheduleThrough(_dispatcher.position(), _queue);
    }

    auto stop(bool resetExternal = true) -> void {
        _queue.clear();
        const auto note = _sequencer.stop();
        _dispatcher.stop(note);
        _internalTiming = false;
        if (resetExternal) {
            _external.reset();
        }
    }

    Sequencer& _sequencer;
    MidiClockSettings& _settings;
    MidiEventQueue _queue;
    Transport _transport;
    ExternalMidiClock _external;
    MidiDispatcher _dispatcher;
    bool _internalTiming = false;
    bool _storageOpen = false;
};

} // namespace SwingMetro
