#pragma once

#include "external_midi_clock.h"
#include "internal_tick_source.h"
#include "midi_clock_mode.h"
#include "midi_event_queue.h"
#include "midi_usb_packet.h"
#include "sequencer.h"
#include "timestamp.h"
#include "transport.h"

#include <cstdint>
#include <optional>

namespace SwingMetro {

class MidiPacketSink {
  public:
    virtual ~MidiPacketSink() = default;
    virtual auto send(const MidiUsbPacket& packet) -> void = 0;
};

struct TransportDiagnostics {
    // Maxima are microseconds from local observations, not USB acceptance or host delivery.
    std::uint32_t maxServiceIntervalUs = 0;
    std::uint32_t maxInternalTickProcessingLatenessUs = 0;
    std::uint32_t maxExternalTickProcessingLatenessUs = 0;
    std::uint32_t maxClockAttemptLatenessUs = 0;
    std::uint32_t maxQueuedEventAttemptLatenessUs = 0;
    // Historical count of internal tick records discarded after the per-pass processing budget.
    std::uint32_t droppedTicks = 0;
    std::uint32_t successfulInternalTickPops = 0;
    std::uint32_t outgoingInternalClockAttempts = 0;
    std::uint32_t maxInternalTicksPoppedPerProcessPass = 0;
    std::uint32_t internalTickBudgetReachedPasses = 0;
    std::uint32_t maxRemainingInternalTicksAfterBudgetPass = 0;
    std::uint32_t maxProcessDurationUs = 0;
};

struct TickPipelineDiagnostics {
    // This is a coherent producer snapshot followed by core-0-owned consumer fields.
    InternalTickDiagnostics producer;
    std::uint32_t successfulConsumerPops = 0;
    std::uint32_t budgetDiscards = 0;
    std::uint32_t outgoingInternalClockAttempts = 0;
};

class MidiDispatcher {
  public:
    MidiDispatcher(MidiEventQueue& queue, Transport& transport, Sequencer& sequencer,
                   MidiPacketSink& sink, TransportDiagnostics& diagnostics) noexcept
        : _queue{queue}, _transport{transport}, _sequencer{sequencer}, _sink{sink},
          _diagnostics{diagnostics} {}

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

    auto dispatchDue(std::uint32_t dueAtUs, std::uint32_t attemptAtUs) -> void {
        if (!_haveTick || !_transport.snapshot().running) {
            return;
        }
        if (!timestampReached(dueAtUs, _tickStartUs)) {
            return;
        }
        const auto elapsed = dueAtUs - _tickStartUs;
        const auto phase =
            elapsed >= _tickPeriodUs
                ? PHASE_MAX
                : static_cast<TransportPhase>((static_cast<std::uint64_t>(elapsed) *
                                               (static_cast<std::uint32_t>(PHASE_MAX) + 1U)) /
                                              _tickPeriodUs);
        sendDue({_transport.position().tick, phase}, attemptAtUs);
    }

    auto consumeTick(TransportTickRecord record, bool emitClock, std::uint32_t attemptAtUs)
        -> void {
        if (!_transport.snapshot().running || record.periodUs == 0) {
            return;
        }
        if (_haveTick) {
            dispatchDue(record.timestampUs, attemptAtUs);
            _queue.discardAt(_transport.position().tick);
            (void)_transport.advanceTick();
        }
        _tickStartUs = record.timestampUs;
        _tickPeriodUs = record.periodUs;
        _haveTick = true;
        _sequencer.notifyBoundaryReached(_transport.position().tick);
        if (emitClock) {
            addOne(_diagnostics.outgoingInternalClockAttempts);
            _sink.send(usbMidiRealTimePacket(0xF8));
            updateLateness(_diagnostics.maxClockAttemptLatenessUs, attemptAtUs, record.timestampUs);
        }
        sendDue({_transport.position().tick, 0}, attemptAtUs);
    }

    [[nodiscard]] auto position() const noexcept -> TransportPosition {
        return _transport.position();
    }

  private:
    static auto updateLateness(std::uint32_t& maximum, std::uint32_t attemptAtUs,
                               std::uint32_t deadlineUs) noexcept -> void {
        if (timestampReached(attemptAtUs, deadlineUs)) {
            const auto latenessUs = attemptAtUs - deadlineUs;
            if (latenessUs > maximum) {
                maximum = latenessUs;
            }
        }
    }

    static auto addOne(std::uint32_t& counter) noexcept -> void {
        if (counter != UINT32_MAX) {
            ++counter;
        }
    }

    auto sendDue(TransportPosition position, std::uint32_t attemptAtUs) -> void {
        const auto due = _queue.drainAt(position);
        for (std::size_t index = 0; index < due.count; ++index) {
            const auto& event = due.events[index];
            const auto deadlineUs = _tickStartUs + phaseOffsetUs(event.target.phase, _tickPeriodUs);
            if (event.packet[1] == 0xF8) {
                updateLateness(_diagnostics.maxClockAttemptLatenessUs, attemptAtUs, deadlineUs);
            } else {
                updateLateness(_diagnostics.maxQueuedEventAttemptLatenessUs, attemptAtUs,
                               deadlineUs);
            }
            _sink.send(event.packet);
            const auto& packet = event.packet;
            if ((packet[1] & 0xF0U) == 0x90U && packet[3] != 0U) {
                _sequencer.notifyNoteOnSent(packet[2]);
            } else if ((packet[1] & 0xF0U) == 0x80U ||
                       ((packet[1] & 0xF0U) == 0x90U && packet[3] == 0U)) {
                _sequencer.notifyNoteOffSent(packet[2]);
            }
        }
    }

    MidiEventQueue& _queue;
    Transport& _transport;
    Sequencer& _sequencer;
    MidiPacketSink& _sink;
    TransportDiagnostics& _diagnostics;
    std::uint32_t _tickStartUs = 0;
    std::uint32_t _tickPeriodUs = 0;
    bool _haveTick = false;
    bool _internalOutputActive = false;
};

class TransportController {
  public:
    TransportController(Sequencer& sequencer, MidiClockSettings& settings,
                        MidiPacketSink& sink) noexcept
        : _sequencer{sequencer}, _settings{settings}, _queue{_ownedQueue},
          _dispatcher{_queue, _transport, sequencer, sink, _diagnostics} {}

    TransportController(Sequencer& sequencer, MidiClockSettings& settings, MidiPacketSink& sink,
                        MidiEventQueue& queue) noexcept
        : _sequencer{sequencer}, _settings{settings}, _queue{queue},
          _dispatcher{_queue, _transport, sequencer, sink, _diagnostics} {}

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
        stop(InternalTickDiscardReason::ModeSwitch);
        _external.reset();
        _settings.apply(mode);
    }

    auto openStorage() -> void {
        _storageOpen = true;
        stop(InternalTickDiscardReason::Storage);
    }

    auto closeStorage() noexcept -> void { _storageOpen = false; }

    auto handleExternal(const MidiRealtimeEvent& event, std::uint32_t observedAtUs) -> void {
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
            stop(InternalTickDiscardReason::Stop, false);
        } else if (result.tick) {
            updateTickLateness(_diagnostics.maxExternalTickProcessingLatenessUs, observedAtUs,
                               result.tickRecord.timestampUs);
            _dispatcher.consumeTick(result.tickRecord, false, observedAtUs);
        }
    }

    auto process(std::uint32_t nowUs, InternalTickStore<>& ticks) -> void {
        updateServiceInterval(nowUs);
        if (_storageOpen) {
            (void)ticks.discard();
            return;
        }
        if (_internalTiming) {
            TransportTickRecord record;
            std::uint8_t processed = 0;
            while (processed < MAX_INTERNAL_TICKS_PER_PASS && ticks.pop(record)) {
                addOne(_diagnostics.successfulInternalTickPops);
                updateTickLateness(_diagnostics.maxInternalTickProcessingLatenessUs, nowUs,
                                   record.timestampUs);
                _dispatcher.consumeTick(record, _settings.mode() == MidiClockMode::Internal, nowUs);
                ++processed;
            }
            updateMaximum(_diagnostics.maxInternalTicksPoppedPerProcessPass, processed);
            if (processed == MAX_INTERNAL_TICKS_PER_PASS) {
                addOne(_diagnostics.internalTickBudgetReachedPasses);
                updateMaximum(_diagnostics.maxRemainingInternalTicksAfterBudgetPass, ticks.size());
            }
        } else {
            (void)ticks.discard();
        }
        _dispatcher.dispatchDue(nowUs, nowUs);
        schedule();
        if (_settings.mode() == MidiClockMode::External) {
            const auto result = _external.update(nowUs);
            if (result.stopped) {
                stop(InternalTickDiscardReason::Stop, false);
            }
        }
    }

    // Call immediately after process() with a timestamp from the hardware-independent caller.
    auto recordProcessDuration(std::uint32_t startedAtUs, std::uint32_t completedAtUs) noexcept
        -> void {
        if (timestampReached(completedAtUs, startedAtUs)) {
            updateMaximum(_diagnostics.maxProcessDurationUs, completedAtUs - startedAtUs);
        }
    }

    [[nodiscard]] auto usesInternalTiming() const noexcept -> bool { return _internalTiming; }
    [[nodiscard]] auto externalStatus() const noexcept -> ExternalMidiClockStatus {
        return _external.status();
    }
    [[nodiscard]] auto externalBpm() const noexcept -> std::uint8_t { return _external.bpm(); }
    [[nodiscard]] auto diagnostics() const noexcept -> TransportDiagnostics { return _diagnostics; }
    [[nodiscard]] auto pipelineDiagnostics(const InternalTickSource& source) const noexcept
        -> TickPipelineDiagnostics {
        return {source.diagnostics(), _diagnostics.successfulInternalTickPops,
                _diagnostics.droppedTicks, _diagnostics.outgoingInternalClockAttempts};
    }
    [[nodiscard]] auto internalTickDiscardReason() const noexcept -> InternalTickDiscardReason {
        return _internalTickDiscardReason;
    }

  private:
    static auto addOne(std::uint32_t& counter) noexcept -> void {
        if (counter != UINT32_MAX) {
            ++counter;
        }
    }

    static auto updateTickLateness(std::uint32_t& maximum, std::uint32_t observedAtUs,
                                   std::uint32_t timestampUs) noexcept -> void {
        if (timestampReached(observedAtUs, timestampUs)) {
            const auto latenessUs = observedAtUs - timestampUs;
            if (latenessUs > maximum) {
                maximum = latenessUs;
            }
        }
    }

    static auto updateMaximum(std::uint32_t& maximum, std::uint32_t value) noexcept -> void {
        if (value > maximum) {
            maximum = value;
        }
    }

    auto updateServiceInterval(std::uint32_t nowUs) noexcept -> void {
        if (_haveServiceTimestamp && timestampReached(nowUs, _lastServiceAtUs)) {
            const auto intervalUs = nowUs - _lastServiceAtUs;
            if (intervalUs > _diagnostics.maxServiceIntervalUs) {
                _diagnostics.maxServiceIntervalUs = intervalUs;
            }
        }
        _lastServiceAtUs = nowUs;
        _haveServiceTimestamp = true;
    }

    auto start(bool waitForExternalTick) -> void {
        _queue.clear();
        const auto note = _sequencer.stop();
        _dispatcher.stop(note);
        _sequencer.start();
        _dispatcher.start(_settings.mode() == MidiClockMode::Internal);
        _internalTiming = !waitForExternalTick;
        _internalTickDiscardReason = InternalTickDiscardReason::Stop;
        schedule();
    }

    auto schedule() -> void {
        if (_sequencer.scheduleThrough(_dispatcher.position(), _queue) !=
            MidiEventQueueEnqueueResult::Ok) {
            stop();
        }
    }

    auto stop(InternalTickDiscardReason discardReason = InternalTickDiscardReason::Stop,
              bool resetExternal = true) -> void {
        _queue.clear();
        const auto note = _sequencer.stop();
        _dispatcher.stop(note);
        _internalTiming = false;
        _internalTickDiscardReason = discardReason;
        if (resetExternal) {
            _external.reset();
        }
    }

    Sequencer& _sequencer;
    MidiClockSettings& _settings;
    MidiEventQueue _ownedQueue;
    MidiEventQueue& _queue;
    Transport _transport;
    ExternalMidiClock _external;
    TransportDiagnostics _diagnostics{};
    MidiDispatcher _dispatcher;
    bool _internalTiming = false;
    bool _storageOpen = false;
    InternalTickDiscardReason _internalTickDiscardReason = InternalTickDiscardReason::Stop;
    std::uint32_t _lastServiceAtUs = 0;
    bool _haveServiceTimestamp = false;
    static constexpr std::uint8_t MAX_INTERNAL_TICKS_PER_PASS = 4;
};

} // namespace SwingMetro
