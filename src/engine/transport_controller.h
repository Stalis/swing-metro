#pragma once

#include "external_midi_clock.h"
#include "internal_tick_source.h"
#include "midi_clock_mode.h"
#include "midi_event_queue.h"
#include "midi_pending_delivery_queue.h"
#include "sequencer.h"
#include "timestamp.h"
#include "transport.h"

#include <cstdint>
#include <optional>

namespace SwingMetro {

enum class SendResult : std::uint8_t {
    Accepted,
    RetryLater,
    Disconnected,
};

class MidiMessageSink {
  public:
    virtual ~MidiMessageSink() = default;
    virtual auto send(const MidiMessage& message) -> SendResult = 0;
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
    static constexpr std::uint8_t MAX_SEND_ATTEMPTS_PER_PASS = 8;

    MidiDispatcher(MidiEventQueue& queue, Transport& transport, Sequencer& sequencer,
                   MidiMessageSink& sink, TransportDiagnostics& diagnostics) noexcept
        : _queue{queue}, _transport{transport}, _sequencer{sequencer}, _sink{sink},
          _diagnostics{diagnostics} {}

    auto beginPass(std::uint32_t attemptAtUs = 0) -> void {
        _attemptsRemaining = MAX_SEND_ATTEMPTS_PER_PASS;
        _passActive = true;
        _deliveryBlocked = false;
        servicePending(attemptAtUs);
    }

    auto finishPass(std::uint32_t attemptAtUs) -> void {
        servicePending(attemptAtUs);
        _passActive = false;
    }

    auto start(bool emitStart, std::uint32_t deadlineUs = 0) -> void {
        _transport.start();
        _haveTick = false;
        _internalOutputActive = emitStart;
        if (emitStart) {
            enqueueDirect(MidiMessage::start(), deadlineUs);
        }
        serviceWhenStandalone(deadlineUs);
    }

    auto continuePlayback() noexcept -> void {
        _transport.continuePlayback();
        _haveTick = false;
    }

    auto stop(std::optional<MIDI_Note> note, std::uint32_t deadlineUs) -> void {
        if (note.has_value()) {
            enqueueDirect(*MidiMessage::noteOff(0, *note), deadlineUs);
        }
        if (_internalOutputActive) {
            enqueueDirect(MidiMessage::stop(), deadlineUs);
        }
        _internalOutputActive = false;
        if (_haveTick) {
            (void)_transport.advanceTick();
        }
        _haveTick = false;
        _transport.stop();
        serviceWhenStandalone(deadlineUs);
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
        serviceWhenStandalone(attemptAtUs);
    }

    auto consumeTick(TransportTickRecord record, bool emitClock, std::uint32_t attemptAtUs)
        -> void {
        if (!_transport.snapshot().running || record.periodUs == 0) {
            return;
        }
        if (_haveTick) {
            dispatchDue(record.timestampUs, attemptAtUs);
            if (_capacityExceeded) {
                return;
            }
            _queue.discardAt(_transport.position().tick);
            (void)_transport.advanceTick();
        }
        _tickStartUs = record.timestampUs;
        _tickPeriodUs = record.periodUs;
        _haveTick = true;
        _sequencer.notifyBoundaryReached(_transport.position().tick);
        if (emitClock) {
            enqueueDirect(MidiMessage::clock(), record.timestampUs, true);
        }
        sendDue({_transport.position().tick, 0}, attemptAtUs);
        serviceWhenStandalone(attemptAtUs);
    }

    [[nodiscard]] auto position() const noexcept -> TransportPosition {
        return _transport.position();
    }

    [[nodiscard]] auto capacityExceeded() const noexcept -> bool { return _capacityExceeded; }
    [[nodiscard]] auto pendingFront() const noexcept -> std::optional<PendingMidiEvent> {
        return _pending.front();
    }

    auto abortOutboundSession() noexcept -> void {
        _pending.clear();
        _capacityExceeded = false;
        _attemptsRemaining = 0;
        _passActive = false;
        _deliveryBlocked = false;
        _haveTick = false;
        _internalOutputActive = false;
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

    auto enqueueDirect(const MidiMessage& message, std::uint32_t deadlineUs,
                       bool countsAsInternalClockAttempt = false) -> void {
        const MidiEvent event{{_transport.position().tick, 0}, message, 0};
        enqueuePending(event, deadlineUs, countsAsInternalClockAttempt,
                       countsAsInternalClockAttempt ? MidiAttemptLateness::ClockAttempt
                                                    : MidiAttemptLateness::None);
    }

    auto enqueuePending(const MidiEvent& event, std::uint32_t deadlineUs,
                        bool countsAsInternalClockAttempt = false,
                        MidiAttemptLateness lateness = MidiAttemptLateness::QueuedEventAttempt)
        -> void {
        const auto candidate = _nextDeliverySequenceNumber;
        if (!_pending.push(event, deadlineUs, candidate, countsAsInternalClockAttempt, lateness)) {
            _capacityExceeded = true;
            return;
        }
        ++_nextDeliverySequenceNumber;
    }

    auto sendDue(TransportPosition position, std::uint32_t) -> void {
        while (const auto event = _queue.front()) {
            if (event->target > position) {
                return;
            }
            const auto deadlineUs =
                _tickStartUs + phaseOffsetUs(event->target.phase, _tickPeriodUs);
            enqueuePending(*event, deadlineUs, false,
                           event->message.isClock() ? MidiAttemptLateness::ClockAttempt
                                                    : MidiAttemptLateness::QueuedEventAttempt);
            if (_capacityExceeded) {
                return;
            }
            _queue.popFront();
        }
    }

    auto serviceWhenStandalone(std::uint32_t attemptAtUs) -> void {
        if (_passActive) {
            return;
        }
        _attemptsRemaining = MAX_SEND_ATTEMPTS_PER_PASS;
        _deliveryBlocked = false;
        servicePending(attemptAtUs);
    }

    auto servicePending(std::uint32_t attemptAtUs) -> void {
        if (_deliveryBlocked) {
            return;
        }
        while (_attemptsRemaining > 0) {
            const auto pending = _pending.front();
            if (!pending.has_value()) {
                return;
            }
            if (pending->lateness == MidiAttemptLateness::ClockAttempt) {
                updateLateness(_diagnostics.maxClockAttemptLatenessUs, attemptAtUs,
                               pending->deadlineUs);
            } else if (pending->lateness == MidiAttemptLateness::QueuedEventAttempt) {
                updateLateness(_diagnostics.maxQueuedEventAttemptLatenessUs, attemptAtUs,
                               pending->deadlineUs);
            }
            if (pending->countsAsInternalClockAttempt) {
                addOne(_diagnostics.outgoingInternalClockAttempts);
            }
            --_attemptsRemaining;
            if (_sink.send(pending->event.message) != SendResult::Accepted) {
                _deliveryBlocked = true;
                return;
            }
            if (pending->event.message.isNoteOn()) {
                _sequencer.notifyNoteOnAccepted(pending->event.message.note());
            } else if (pending->event.message.isNoteOffEquivalent()) {
                _sequencer.notifyNoteOffAccepted(pending->event.message.note());
            }
            _pending.popFront();
        }
    }

    MidiEventQueue& _queue;
    Transport& _transport;
    Sequencer& _sequencer;
    MidiMessageSink& _sink;
    TransportDiagnostics& _diagnostics;
    std::uint32_t _tickStartUs = 0;
    std::uint32_t _tickPeriodUs = 0;
    MidiPendingDeliveryQueue _pending;
    std::size_t _nextDeliverySequenceNumber = 0;
    std::uint8_t _attemptsRemaining = 0;
    bool _passActive = false;
    bool _deliveryBlocked = false;
    bool _capacityExceeded = false;
    bool _haveTick = false;
    bool _internalOutputActive = false;
};

class TransportController {
  public:
    TransportController(Sequencer& sequencer, MidiClockSettings& settings,
                        MidiMessageSink& sink) noexcept
        : _sequencer{sequencer}, _settings{settings}, _queue{_ownedQueue},
          _dispatcher{_queue, _transport, sequencer, sink, _diagnostics} {}

    TransportController(Sequencer& sequencer, MidiClockSettings& settings, MidiMessageSink& sink,
                        MidiEventQueue& queue) noexcept
        : _sequencer{sequencer}, _settings{settings}, _queue{queue},
          _dispatcher{_queue, _transport, sequencer, sink, _diagnostics} {}

    auto toggle(std::uint32_t nowUs) -> void {
        _dispatcher.beginPass(nowUs);
        if (_sequencer.isRunning()) {
            stop(InternalTickDiscardReason::Stop, true, nowUs);
        } else {
            start(_settings.mode() == MidiClockMode::External, nowUs);
        }
        finishPass(nowUs);
    }

    auto applyMode(MidiClockMode mode, std::uint32_t nowUs = 0) -> void {
        if (mode == _settings.mode()) {
            return;
        }
        _dispatcher.beginPass(nowUs);
        stop(InternalTickDiscardReason::ModeSwitch, true, nowUs);
        _external.reset();
        _settings.apply(mode);
        finishPass(nowUs);
    }

    auto openStorage(std::uint32_t nowUs = 0) -> void {
        _storageOpen = true;
        _dispatcher.beginPass(nowUs);
        stop(InternalTickDiscardReason::Storage, true, nowUs);
        finishPass(nowUs);
    }

    auto closeStorage() noexcept -> void { _storageOpen = false; }

    auto handleExternal(const MidiRealtimeEvent& event, std::uint32_t observedAtUs) -> void {
        if (_storageOpen || _settings.mode() != MidiClockMode::External) {
            return;
        }
        _dispatcher.beginPass(observedAtUs);
        const auto result = _external.handle(event);
        if (result.reset) {
            start(true, observedAtUs);
        } else if (result.started && !_sequencer.isRunning()) {
            _sequencer.continuePlayback();
            _transport.continuePlayback();
            _dispatcher.continuePlayback();
        }
        if (result.stopped) {
            stop(InternalTickDiscardReason::Stop, false, observedAtUs);
        } else if (result.tick) {
            updateTickLateness(_diagnostics.maxExternalTickProcessingLatenessUs, observedAtUs,
                               result.tickRecord.timestampUs);
            _dispatcher.consumeTick(result.tickRecord, false, observedAtUs);
        }
        finishPass(observedAtUs);
    }

    auto process(std::uint32_t nowUs, InternalTickStore<>& ticks) -> void {
        updateServiceInterval(nowUs);
        if (_storageOpen) {
            (void)ticks.discard();
            return;
        }
        _dispatcher.beginPass(nowUs);
        if (_internalTiming) {
            TransportTickRecord record;
            std::uint8_t processed = 0;
            while (processed < MAX_INTERNAL_TICKS_PER_PASS && ticks.pop(record)) {
                addOne(_diagnostics.successfulInternalTickPops);
                updateTickLateness(_diagnostics.maxInternalTickProcessingLatenessUs, nowUs,
                                   record.timestampUs);
                _dispatcher.consumeTick(record, _settings.mode() == MidiClockMode::Internal, nowUs);
                if (_dispatcher.capacityExceeded()) {
                    stopForDeliveryCapacity();
                    break;
                }
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
        if (_dispatcher.capacityExceeded()) {
            stopForDeliveryCapacity();
        } else {
            schedule(nowUs);
        }
        if (_settings.mode() == MidiClockMode::External) {
            const auto result = _external.update(nowUs);
            if (result.stopped) {
                stop(InternalTickDiscardReason::Stop, false, nowUs);
            }
        }
        finishPass(nowUs);
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

    auto start(bool waitForExternalTick, std::uint32_t nowUs) -> void {
        _queue.clear();
        const auto note = _sequencer.stop();
        _dispatcher.stop(note, nowUs);
        _sequencer.start();
        _dispatcher.start(_settings.mode() == MidiClockMode::Internal, nowUs);
        _internalTiming = !waitForExternalTick;
        _internalTickDiscardReason = InternalTickDiscardReason::Stop;
        schedule(nowUs);
    }

    auto schedule(std::uint32_t nowUs) -> void {
        if (_sequencer.scheduleThrough(_dispatcher.position(), _queue) !=
            MidiEventQueueEnqueueResult::Ok) {
            stop(InternalTickDiscardReason::Stop, true, nowUs);
        }
    }

    auto stop(InternalTickDiscardReason discardReason = InternalTickDiscardReason::Stop,
              bool resetExternal = true, std::uint32_t nowUs = 0) -> void {
        _queue.clear();
        const auto note = _sequencer.stop();
        _dispatcher.stop(note, nowUs);
        _internalTiming = false;
        _internalTickDiscardReason = discardReason;
        if (resetExternal) {
            _external.reset();
        }
    }

    auto finishPass(std::uint32_t nowUs) -> void {
        if (_dispatcher.capacityExceeded()) {
            stopForDeliveryCapacity();
        } else {
            _dispatcher.finishPass(nowUs);
        }
    }

    auto stopForDeliveryCapacity() noexcept -> void {
        _queue.clear();
        _dispatcher.abortOutboundSession();
        (void)_sequencer.stop();
        _sequencer.cancelRequestedNoteOff();
        _transport.stop();
        _internalTiming = false;
        _internalTickDiscardReason = InternalTickDiscardReason::Stop;
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
