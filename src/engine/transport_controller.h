#pragma once

#include "external_midi_clock.h"
#include "internal_tick_source.h"
#include "midi_clock_mode.h"
#include "midi_event_queue.h"
#include "midi_pending_delivery_queue.h"
#include "sequencer.h"
#include "stage5_instrumentation.h"
#include "timestamp.h"
#include "transport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace SwingMetro {

enum class SendResult : std::uint8_t {
    Accepted,
    RetryLater,
    Disconnected,
};

using DeliveryMessageClass = MidiMessageClass;
static constexpr std::size_t DELIVERY_MESSAGE_CLASS_COUNT = MIDI_MESSAGE_CLASS_COUNT;

struct LatenessDistribution {
    static constexpr std::size_t POSITIVE_BUCKET_COUNT = 10;
    std::uint32_t early = 0;
    std::uint32_t onTime = 0;
    std::uint32_t unordered = 0;
    std::array<std::uint32_t, POSITIVE_BUCKET_COUNT> positive{};
};

struct DeliveryClassDiagnostics {
    std::uint32_t attempts = 0;
    std::uint32_t accepted = 0;
    std::uint32_t retryLater = 0;
    std::uint32_t disconnected = 0;
    std::uint32_t retryRecovered = 0;
    std::uint32_t maxFirstAttemptLatenessUs = 0;
    std::uint32_t maxAcceptanceLatenessUs = 0;
};

struct MidiDeliveryAttempt {
    MidiMessage message{};
    std::size_t deliverySequenceNumber = 0;
    std::uint32_t sessionGeneration = 1;
    TransportPosition target{};
    std::uint32_t deadlineUs = 0;
    std::uint8_t attemptOrdinal = 0;
    bool firstAttempt = true;
    MidiLaunchId launchId = 0;
};

class MidiMessageSink {
  public:
    virtual ~MidiMessageSink() = default;
    virtual auto send(const MidiDeliveryAttempt& attempt) -> SendResult = 0;
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
    std::uint32_t clockCoalescedCount = 0;
    std::uint32_t clockExpiredCount = 0;
    std::uint32_t noteOnExpiredCount = 0;
    std::uint32_t staleGateOffCount = 0;
    std::uint32_t terminalNoteOffAbandonedCount = 0;
    std::uint32_t terminalStopAbandonedCount = 0;
    std::array<DeliveryClassDiagnostics, DELIVERY_MESSAGE_CLASS_COUNT> delivery{};
    std::array<std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT>,
               DELIVERY_REMOVAL_REASON_COUNT>
        pendingRemovals{};
    std::array<std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT>,
               DELIVERY_REMOVAL_REASON_COUNT>
        scheduledRemovals{};
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> scheduledCreated{};
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> scheduledTransferred{};
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> outboxInserted{};
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> currentScheduledDepth{};
#if SWING_METRO_STAGE5_INSTRUMENTATION
    std::uint32_t currentScheduledDepthTotal = 0;
    std::uint32_t maxScheduledDepth = 0;
#endif
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> currentOutboxDepthByClass{};
    std::uint32_t currentOutboxDepth = 0;
    std::uint32_t maxOutboxDepth = 0;
    std::uint32_t maxSendAttemptsPerPublicPass = 0;
    std::uint32_t outboxCapacityFailures = 0;
    std::uint32_t deliveryCapacitySafetyStops = 0;
    std::uint32_t retryWindowSafetyStops = 0;
    std::uint32_t sessionGenerationAdvances = 0;
    std::uint32_t explicitCleanStarts = 0;
    std::array<std::uint32_t, static_cast<std::size_t>(InvalidationReason::SupersededStart) + 1>
        sessionEnds{};
    std::uint32_t currentSessionGeneration = 1;
    InvalidationReason lastSessionEndReason = InvalidationReason::Stop;
#if SWING_METRO_STAGE5_INSTRUMENTATION
    LatenessDistribution clockAttemptLateness{};
    LatenessDistribution clockAcceptedLateness{};
    LatenessDistribution noteAttemptLateness{};
    LatenessDistribution noteAcceptedLateness{};
#endif
};

enum class DeliveryLifecycleOutcome : std::uint8_t {
    None,
    Disconnected,
    RetryWindowExceeded,
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
        _attemptsThisPass = 0;
        _passActive = true;
        _deliveryBlocked = false;
        servicePending(attemptAtUs);
    }

    auto finishPass(std::uint32_t attemptAtUs) -> void {
        servicePending(attemptAtUs);
        _passActive = false;
    }

    auto start(bool emitStart, std::uint32_t deadlineUs = 0) -> void {
        recordPendingRemoval(_pending.removeIf([](const PendingMidiEvent& pending) {
            return pending.terminal && pending.event.message.type() == MidiMessageType::Stop;
        }),
                             DeliveryRemovalReason::SupersededStart);
        advanceSessionGeneration();
        addOne(_diagnostics.explicitCleanStarts);
        _disconnected = false;
        _transport.start();
        _haveTick = false;
        _internalOutputActive = emitStart;
        if (emitStart) {
            enqueueDirect(MidiMessage::start(), deadlineUs);
        }
        serviceWhenStandalone(deadlineUs);
    }

    auto continuePlayback() noexcept -> void {
        advanceSessionGeneration();
        _transport.continuePlayback();
        _haveTick = false;
    }

    auto stopLaunch(std::optional<NoteLaunch> launch, std::uint32_t deadlineUs,
                    InvalidationReason reason = InvalidationReason::Stop, bool service = true)
        -> void {
        if (_terminalGeneration != _sessionGeneration) {
            const auto summary = _pending.removeIf([this](const PendingMidiEvent& pending) {
                if (pending.sessionGeneration != _sessionGeneration) {
                    return false;
                }
                const auto type = pending.event.message.type();
                return pending.event.message.isClock() || pending.event.message.isNoteOn() ||
                       type == MidiMessageType::Start || type == MidiMessageType::Continue;
            });
            recordPendingRemoval(summary, deliveryRemovalReason(reason));
            if (launch.has_value() &&
                !_pending.hasNoteOff(launch->sessionGeneration, launch->launchId)) {
                const MidiEvent event{{_transport.position().tick, 0},
                                      *MidiMessage::noteOff(0, launch->note),
                                      0,
                                      launch->launchId,
                                      launch->sessionGeneration};
                enqueuePending(event, deadlineUs, false, MidiAttemptLateness::None, true);
            }
            if (_internalOutputActive &&
                !_pending.hasTerminal(MidiMessageType::Stop, _sessionGeneration)) {
                enqueueDirect(MidiMessage::stop(), deadlineUs, false, MidiAttemptLateness::None,
                              true);
            }
            _terminalGeneration = _sessionGeneration;
            _diagnostics.lastSessionEndReason = reason;
            addOne(_diagnostics.sessionEnds[static_cast<std::size_t>(reason)]);
        }
        _internalOutputActive = false;
        _clockRetryStartTick.reset();
        if (_haveTick) {
            (void)_transport.advanceTick();
        }
        _haveTick = false;
        _transport.stop();
        if (service) {
            serviceWhenStandalone(deadlineUs);
        }
    }

    auto stop(std::optional<MIDI_Note> note, std::uint32_t deadlineUs,
              InvalidationReason reason = InvalidationReason::Stop, bool service = true) -> void {
        const auto launch = note.has_value()
                                ? std::optional<NoteLaunch>{{*note, 0, _sessionGeneration}}
                                : std::nullopt;
        stopLaunch(launch, deadlineUs, reason, service);
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
            recordScheduledRemoval(_queue.discardAt(_transport.position().tick),
                                   DeliveryRemovalReason::ScheduledOverdue);
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

    [[nodiscard]] auto takeLifecycleOutcome() noexcept -> DeliveryLifecycleOutcome {
        const auto outcome = _lifecycleOutcome;
        _lifecycleOutcome = DeliveryLifecycleOutcome::None;
        return outcome;
    }

    [[nodiscard]] auto abandonPending() noexcept -> PendingRemovalSummary {
        const auto summary = _pending.removeIf([](const PendingMidiEvent&) { return true; });
        recordPendingRemoval(summary, DeliveryRemovalReason::Disconnected);
        addCount(_diagnostics.terminalNoteOffAbandonedCount, summary.terminalNoteOffs);
        addCount(_diagnostics.terminalStopAbandonedCount, summary.terminalStops);
        _diagnostics.lastSessionEndReason = InvalidationReason::Disconnected;
        addOne(
            _diagnostics.sessionEnds[static_cast<std::size_t>(InvalidationReason::Disconnected)]);
        _terminalGeneration = 0;
        _disconnected = false;
        return summary;
    }

    [[nodiscard]] auto hasTerminalNoteOff() const noexcept -> bool {
        return _pending.hasTerminalNoteOff();
    }

    [[nodiscard]] auto sessionGeneration() const noexcept -> std::uint32_t {
        return _sessionGeneration;
    }

    auto clearCapacityExceeded() noexcept -> void { _capacityExceeded = false; }

    auto recordScheduledRemoval(const MidiMessageClassSummary& summary,
                                DeliveryRemovalReason reason) noexcept -> void {
        addRemoval(_diagnostics.scheduledRemovals, summary, reason);
        updateScheduledDepth();
    }

    auto recordScheduledDepth() noexcept -> void { updateScheduledDepth(); }

    auto abortOutboundSession() noexcept -> void {
        (void)abandonPending();
        _capacityExceeded = false;
        _attemptsRemaining = 0;
        _passActive = false;
        _deliveryBlocked = false;
        _haveTick = false;
        _internalOutputActive = false;
    }

  private:
    [[nodiscard]] static auto index(MidiMessageClass messageClass) noexcept -> std::size_t {
        return static_cast<std::size_t>(messageClass);
    }

    static auto addSummary(std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT>& destination,
                           const MidiMessageClassSummary& summary) noexcept -> void {
        for (std::size_t messageClass = 0; messageClass < DELIVERY_MESSAGE_CLASS_COUNT;
             ++messageClass) {
            addCount(destination[messageClass], summary.counts[messageClass]);
        }
    }

    static auto addRemoval(std::array<std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT>,
                                      DELIVERY_REMOVAL_REASON_COUNT>& destination,
                           const MidiMessageClassSummary& summary,
                           DeliveryRemovalReason reason) noexcept -> void {
        addSummary(destination[static_cast<std::size_t>(reason)], summary);
    }

    auto recordPendingRemoval(const PendingRemovalSummary& summary,
                              DeliveryRemovalReason reason) noexcept -> void {
        addRemoval(_diagnostics.pendingRemovals, summary.byClass, reason);
        updateOutboxDepth();
    }

    auto updateOutboxDepth() noexcept -> void {
        const auto summary = _pending.classSummary();
        for (std::size_t messageClass = 0; messageClass < DELIVERY_MESSAGE_CLASS_COUNT;
             ++messageClass) {
            _diagnostics.currentOutboxDepthByClass[messageClass] =
                static_cast<std::uint32_t>(summary.counts[messageClass]);
        }
        _diagnostics.currentOutboxDepth = static_cast<std::uint32_t>(_pending.size());
        if (_diagnostics.currentOutboxDepth > _diagnostics.maxOutboxDepth) {
            _diagnostics.maxOutboxDepth = _diagnostics.currentOutboxDepth;
        }
    }

    auto updateScheduledDepth() noexcept -> void {
        const auto summary = _queue.classSummary();
        for (std::size_t messageClass = 0; messageClass < DELIVERY_MESSAGE_CLASS_COUNT;
             ++messageClass) {
            _diagnostics.currentScheduledDepth[messageClass] =
                static_cast<std::uint32_t>(summary.counts[messageClass]);
        }
#if SWING_METRO_STAGE5_INSTRUMENTATION
        _diagnostics.currentScheduledDepthTotal = static_cast<std::uint32_t>(_queue.size());
        if (_diagnostics.currentScheduledDepthTotal > _diagnostics.maxScheduledDepth) {
            _diagnostics.maxScheduledDepth = _diagnostics.currentScheduledDepthTotal;
        }
#endif
    }

    static auto recordLateness(LatenessDistribution& distribution, std::uint32_t attemptAtUs,
                               std::uint32_t deadlineUs) noexcept -> void {
#if SWING_METRO_STAGE5_INSTRUMENTATION
        const auto difference = attemptAtUs - deadlineUs;
        if (difference == 0) {
            addOne(distribution.onTime);
            return;
        }
        if (difference == TIMESTAMP_COMPARISON_HORIZON_US) {
            addOne(distribution.unordered);
            return;
        }
        if (!timestampReached(attemptAtUs, deadlineUs)) {
            addOne(distribution.early);
            return;
        }
        constexpr std::array<std::uint32_t, LatenessDistribution::POSITIVE_BUCKET_COUNT - 1>
            upperBounds = {10, 50, 100, 250, 500, 1'000, 5'000, 20'000, 100'000};
        std::size_t bucket = 0;
        while (bucket < upperBounds.size() && difference > upperBounds[bucket]) {
            ++bucket;
        }
        addOne(distribution.positive[bucket]);
#else
        (void)distribution;
        (void)attemptAtUs;
        (void)deadlineUs;
#endif
    }

    auto recordDistributionAttempt(const PendingMidiEvent& pending,
                                   std::uint32_t attemptAtUs) noexcept -> void {
#if SWING_METRO_STAGE5_INSTRUMENTATION
        if (pending.event.message.isClock()) {
            recordLateness(_diagnostics.clockAttemptLateness, attemptAtUs, pending.deadlineUs);
        } else if (pending.event.message.messageClass() == MidiMessageClass::Note) {
            recordLateness(_diagnostics.noteAttemptLateness, attemptAtUs, pending.deadlineUs);
        }
#else
        (void)pending;
        (void)attemptAtUs;
#endif
    }

    auto recordDistributionAccepted(const PendingMidiEvent& pending,
                                    std::uint32_t attemptAtUs) noexcept -> void {
#if SWING_METRO_STAGE5_INSTRUMENTATION
        if (pending.event.message.isClock()) {
            recordLateness(_diagnostics.clockAcceptedLateness, attemptAtUs, pending.deadlineUs);
        } else if (pending.event.message.messageClass() == MidiMessageClass::Note) {
            recordLateness(_diagnostics.noteAcceptedLateness, attemptAtUs, pending.deadlineUs);
        }
#else
        (void)pending;
        (void)attemptAtUs;
#endif
    }
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

    static auto addCount(std::uint32_t& counter, std::size_t count) noexcept -> void {
        const auto available = static_cast<std::size_t>(UINT32_MAX - counter);
        counter += static_cast<std::uint32_t>(count > available ? available : count);
    }

    auto enqueueDirect(const MidiMessage& message, std::uint32_t deadlineUs,
                       bool countsAsInternalClockAttempt = false,
                       MidiAttemptLateness lateness = MidiAttemptLateness::None,
                       bool terminal = false) -> void {
        const MidiEvent event{{_transport.position().tick, 0}, message, 0};
        enqueuePending(event, deadlineUs, countsAsInternalClockAttempt,
                       countsAsInternalClockAttempt ? MidiAttemptLateness::ClockAttempt : lateness,
                       terminal);
    }

    auto enqueuePending(MidiEvent event, std::uint32_t deadlineUs,
                        bool countsAsInternalClockAttempt = false,
                        MidiAttemptLateness lateness = MidiAttemptLateness::QueuedEventAttempt,
                        bool terminal = false) -> bool {
        if (event.sessionGeneration == 0) {
            event.sessionGeneration = _sessionGeneration;
        }
        if (event.message.isClock()) {
            const auto summary = _pending.removeIf([this](const PendingMidiEvent& pending) {
                return pending.sessionGeneration == _sessionGeneration &&
                       pending.event.message.isClock();
            });
            recordPendingRemoval(summary, DeliveryRemovalReason::ClockCoalesced);
            addCount(_diagnostics.clockCoalescedCount,
                     summary.byClass.counts[static_cast<std::size_t>(MidiMessageClass::Clock)]);
        }
        const auto candidate = _nextDeliverySequenceNumber;
        const auto inserted =
            terminal
                ? _pending.push(event, deadlineUs, candidate, countsAsInternalClockAttempt,
                                lateness, _sessionGeneration, true)
                : _pending.pushNormal(event, deadlineUs, candidate, countsAsInternalClockAttempt,
                                      lateness, _sessionGeneration);
        if (!inserted) {
            addOne(_diagnostics.outboxCapacityFailures);
            _capacityExceeded = true;
            return false;
        }
        ++_nextDeliverySequenceNumber;
        addOne(_diagnostics.outboxInserted[index(event.message.messageClass())]);
        updateOutboxDepth();
        return true;
    }

    auto sendDue(TransportPosition position, std::uint32_t) -> void {
        _deliveryPosition = position;
        while (const auto event = _queue.front()) {
            if (event->target > position) {
                return;
            }
            if (event->sessionGeneration != 0 && event->sessionGeneration != _sessionGeneration) {
                recordScheduledRemoval(_queue.removeIf([event](const MidiEvent& candidate) {
                    return candidate.sessionGeneration == event->sessionGeneration &&
                           candidate.launchId == event->launchId;
                }),
                                       DeliveryRemovalReason::StaleGateOff);
                addOne(_diagnostics.staleGateOffCount);
                continue;
            }
            if (event->message.isNoteOn() && event->gateDeadline != TransportPosition{} &&
                event->gateDeadline <= position) {
                _sequencer.notifyNoteOnExpired(
                    {event->message.note(), event->launchId, event->sessionGeneration});
                recordScheduledRemoval(_queue.removeIf([event](const MidiEvent& candidate) {
                    return candidate.sessionGeneration == event->sessionGeneration &&
                           candidate.launchId == event->launchId;
                }),
                                       DeliveryRemovalReason::NoteOnExpired);
                addOne(_diagnostics.noteOnExpiredCount);
                continue;
            }
            if (event->message.isNoteOn() && !queueReplacementOff(*event, position)) {
                return;
            }
            const auto deadlineUs =
                _tickStartUs + phaseOffsetUs(event->target.phase, _tickPeriodUs);
            if (!enqueuePending(*event, deadlineUs, false,
                                event->message.isClock()
                                    ? MidiAttemptLateness::ClockAttempt
                                    : MidiAttemptLateness::QueuedEventAttempt)) {
                return;
            }
            if (event->message.isNoteOn()) {
                _sequencer.notifyNoteOnQueued({event->message.note(), event->launchId,
                                               event->sessionGeneration == 0
                                                   ? _sessionGeneration
                                                   : event->sessionGeneration});
            }
            _queue.popFront();
            addOne(_diagnostics.scheduledTransferred[index(event->message.messageClass())]);
            updateScheduledDepth();
        }
    }

    auto serviceWhenStandalone(std::uint32_t attemptAtUs) -> void {
        if (_lifecycleOutcome != DeliveryLifecycleOutcome::None) {
            return;
        }
        if (_passActive) {
            servicePending(attemptAtUs);
            return;
        }
        _attemptsRemaining = MAX_SEND_ATTEMPTS_PER_PASS;
        _attemptsThisPass = 0;
        _deliveryBlocked = false;
        servicePending(attemptAtUs);
    }

    auto servicePending(std::uint32_t attemptAtUs) -> void {
        if (_deliveryBlocked || _disconnected ||
            _lifecycleOutcome != DeliveryLifecycleOutcome::None) {
            return;
        }
        while (_attemptsRemaining > 0) {
            expireOverdue();
            if (_lifecycleOutcome != DeliveryLifecycleOutcome::None) {
                return;
            }
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
            const auto messageClass = pending->event.message.messageClass();
            auto& delivery = _diagnostics.delivery[index(messageClass)];
            const bool firstAttempt = pending->attemptOrdinal == 0;
            if (firstAttempt) {
                updateLateness(delivery.maxFirstAttemptLatenessUs, attemptAtUs,
                               pending->deadlineUs);
            }
            addOne(delivery.attempts);
            ++_attemptsThisPass;
            if (_attemptsThisPass > _diagnostics.maxSendAttemptsPerPublicPass) {
                _diagnostics.maxSendAttemptsPerPublicPass = _attemptsThisPass;
            }
            const MidiDeliveryAttempt attempt{
                pending->event.message,
                pending->deliverySequenceNumber,
                pending->sessionGeneration,
                pending->event.target,
                pending->deadlineUs,
                static_cast<std::uint8_t>(pending->attemptOrdinal == UINT8_MAX
                                              ? UINT8_MAX
                                              : pending->attemptOrdinal + 1U),
                firstAttempt,
                pending->event.launchId};
#if SWING_METRO_STAGE5_INSTRUMENTATION
            recordDistributionAttempt(*pending, attemptAtUs);
#endif
            const auto sendResult = _sink.send(attempt);
            if (sendResult == SendResult::Disconnected) {
                addOne(delivery.disconnected);
                _lifecycleOutcome = DeliveryLifecycleOutcome::Disconnected;
                _disconnected = true;
                _deliveryBlocked = true;
                return;
            }
            if (sendResult == SendResult::RetryLater) {
                addOne(delivery.retryLater);
                _pending.markFrontRetry();
                if (pending->event.message.isClock() && !_clockRetryStartTick.has_value()) {
                    _clockRetryStartTick = pending->event.target.tick;
                }
                _deliveryBlocked = true;
                return;
            }
            addOne(delivery.accepted);
#if SWING_METRO_STAGE5_INSTRUMENTATION
            recordDistributionAccepted(*pending, attemptAtUs);
#endif
            if (pending->retrySeen) {
                addOne(delivery.retryRecovered);
            }
            updateLateness(delivery.maxAcceptanceLatenessUs, attemptAtUs, pending->deadlineUs);
            if (pending->event.message.isClock()) {
                _clockRetryStartTick.reset();
            }
            if (pending->event.message.isNoteOn()) {
                _sequencer.notifyNoteOnAccepted({pending->event.message.note(),
                                                 pending->event.launchId,
                                                 pending->sessionGeneration});
            } else if (pending->event.message.isNoteOffEquivalent()) {
                _sequencer.notifyNoteOffAccepted({pending->event.message.note(),
                                                  pending->event.launchId,
                                                  pending->sessionGeneration});
            }
            _pending.popFront();
            updateOutboxDepth();
        }
    }

    static auto saturatingAddTicks(TransportTick tick, TransportTick ticks) noexcept
        -> TransportTick {
        return tick > UINT64_MAX - ticks ? UINT64_MAX : tick + ticks;
    }

  public:
    [[nodiscard]] static auto expiryTick(TransportTick target) noexcept -> TransportTick {
        return saturatingAddTicks(target, TICKS_PER_SIXTEENTH);
    }

    [[nodiscard]] static auto nextSessionGeneration(std::uint32_t generation) noexcept
        -> std::uint32_t {
        ++generation;
        return generation == 0 ? 1 : generation;
    }

    [[nodiscard]] static auto expiredAt(TransportTick current, TransportTick target) noexcept
        -> bool {
        return current >= expiryTick(target);
    }

  private:
    auto expireOverdue() -> void {
        const auto currentTick = _transport.position().tick;
        if (_clockRetryStartTick.has_value() && expiredAt(currentTick, *_clockRetryStartTick)) {
            _lifecycleOutcome = DeliveryLifecycleOutcome::RetryWindowExceeded;
            return;
        }
        const auto clocks = _pending.removeIf([this, currentTick](const PendingMidiEvent& pending) {
            return pending.sessionGeneration == _sessionGeneration &&
                   pending.event.message.isClock() && currentTick > pending.event.target.tick;
        });
        recordPendingRemoval(clocks, DeliveryRemovalReason::ClockExpired);
        addCount(_diagnostics.clockExpiredCount,
                 clocks.byClass.counts[static_cast<std::size_t>(MidiMessageClass::Clock)]);
        const auto noteOns = _pending.removeIf([this,
                                                currentTick](const PendingMidiEvent& pending) {
            const bool expired = pending.sessionGeneration == _sessionGeneration &&
                                 pending.event.message.isNoteOn() &&
                                 (pending.event.gateDeadline == TransportPosition{}
                                      ? expiredAt(currentTick, pending.event.target.tick)
                                      : _deliveryPosition >= pending.event.gateDeadline);
            if (expired) {
                _sequencer.notifyNoteOnExpired({pending.event.message.note(),
                                                pending.event.launchId, pending.sessionGeneration});
            }
            return expired;
        });
        recordPendingRemoval(noteOns, DeliveryRemovalReason::NoteOnExpired);
        addCount(_diagnostics.noteOnExpiredCount,
                 noteOns.byClass.counts[static_cast<std::size_t>(MidiMessageClass::Note)]);
    }

    auto advanceSessionGeneration() noexcept -> void {
        _sessionGeneration = nextSessionGeneration(_sessionGeneration);
        addOne(_diagnostics.sessionGenerationAdvances);
        _diagnostics.currentSessionGeneration = _sessionGeneration;
        _terminalGeneration = 0;
        _clockRetryStartTick.reset();
    }

    auto queueReplacementOff(const MidiEvent& replacement, TransportPosition position) -> bool {
        const auto active = _sequencer.projectedOrActualLaunch();
        if (!active.has_value() || active->sessionGeneration != _sessionGeneration ||
            active->launchId == replacement.launchId) {
            return true;
        }
        if (_pending.hasNoteOff(active->sessionGeneration, active->launchId)) {
            return true;
        }
        const auto summary = _queue.removeIf([active](const MidiEvent& candidate) {
            return candidate.sessionGeneration == active->sessionGeneration &&
                   candidate.launchId == active->launchId &&
                   candidate.message.isNoteOffEquivalent();
        });
        recordScheduledRemoval(summary, DeliveryRemovalReason::StaleGateOff);
        addCount(_diagnostics.staleGateOffCount,
                 summary.counts[static_cast<std::size_t>(MidiMessageClass::Note)]);
        const MidiEvent off{position,
                            *MidiMessage::noteOff(0, active->note),
                            0,
                            active->launchId,
                            active->sessionGeneration,
                            position};
        return enqueuePending(off, _tickStartUs + phaseOffsetUs(position.phase, _tickPeriodUs),
                              false, MidiAttemptLateness::QueuedEventAttempt, true);
    }

    MidiEventQueue& _queue;
    Transport& _transport;
    Sequencer& _sequencer;
    MidiMessageSink& _sink;
    TransportDiagnostics& _diagnostics;
    std::uint32_t _tickStartUs = 0;
    std::uint32_t _tickPeriodUs = 0;
    TransportPosition _deliveryPosition{};
    MidiPendingDeliveryQueue _pending;
    std::size_t _nextDeliverySequenceNumber = 0;
    std::uint32_t _sessionGeneration = 1;
    std::uint32_t _terminalGeneration = 0;
    std::uint8_t _attemptsRemaining = 0;
    std::uint8_t _attemptsThisPass = 0;
    bool _passActive = false;
    bool _deliveryBlocked = false;
    bool _capacityExceeded = false;
    bool _haveTick = false;
    bool _internalOutputActive = false;
    bool _disconnected = false;
    std::optional<TransportTick> _clockRetryStartTick;
    DeliveryLifecycleOutcome _lifecycleOutcome = DeliveryLifecycleOutcome::None;
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
        stop(InternalTickDiscardReason::ModeSwitch, true, nowUs, true,
             InvalidationReason::ModeSwitch);
        _external.reset();
        _settings.apply(mode);
        finishPass(nowUs);
    }

    auto openStorage(std::uint32_t nowUs = 0) -> void {
        _storageOpen = true;
        _dispatcher.beginPass(nowUs);
        stop(InternalTickDiscardReason::Storage, true, nowUs, true, InvalidationReason::Storage);
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
            _requiresExplicitStart = false;
        } else if (result.started && !_sequencer.isRunning() && !_requiresExplicitStart) {
            _sequencer.continuePlayback();
            _transport.continuePlayback();
            _dispatcher.continuePlayback();
            _sequencer.setSessionGeneration(_dispatcher.sessionGeneration());
        }
        if (result.stopped) {
            stop(InternalTickDiscardReason::Stop, false, observedAtUs, true,
                 InvalidationReason::Stop);
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
                stop(InternalTickDiscardReason::Stop, false, nowUs, true,
                     InvalidationReason::ExternalClockLost);
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
    [[nodiscard]] auto isRunning() const noexcept -> bool { return _transport.snapshot().running; }
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

    static auto addCount(std::uint32_t& counter, std::size_t count) noexcept -> void {
        const auto available = static_cast<std::size_t>(UINT32_MAX - counter);
        counter += static_cast<std::uint32_t>(count > available ? available : count);
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
        _dispatcher.recordScheduledRemoval(_queue.clear(), DeliveryRemovalReason::Stop);
        (void)_sequencer.stop();
        _dispatcher.stopLaunch(_sequencer.actualSoundingLaunch(), nowUs);
        _dispatcher.start(_settings.mode() == MidiClockMode::Internal, nowUs);
        _sequencer.beginCleanRemoteSession(_dispatcher.sessionGeneration());
        _sequencer.start();
        _internalTiming = !waitForExternalTick;
        _requiresExplicitStart = false;
        _internalTickDiscardReason = InternalTickDiscardReason::Stop;
        schedule(nowUs);
    }

    auto schedule(std::uint32_t nowUs) -> void {
        const auto before = _queue.classSummary();
        const auto result = _sequencer.scheduleThrough(_dispatcher.position(), _queue);
        const auto after = _queue.classSummary();
        for (std::size_t messageClass = 0; messageClass < DELIVERY_MESSAGE_CLASS_COUNT;
             ++messageClass) {
            addCount(_diagnostics.scheduledCreated[messageClass],
                     after.counts[messageClass] - before.counts[messageClass]);
        }
        _dispatcher.recordScheduledDepth();
        if (result != MidiEventQueueEnqueueResult::Ok) {
            addOne(_diagnostics.deliveryCapacitySafetyStops);
            stop(InternalTickDiscardReason::Stop, true, nowUs, true,
                 InvalidationReason::DeliveryCapacity);
        }
    }

    auto stop(InternalTickDiscardReason discardReason = InternalTickDiscardReason::Stop,
              bool resetExternal = true, std::uint32_t nowUs = 0, bool service = true,
              InvalidationReason reason = InvalidationReason::Stop) -> void {
        _dispatcher.recordScheduledRemoval(_queue.clear(), deliveryRemovalReason(reason));
        (void)_sequencer.stop();
        _dispatcher.stopLaunch(_sequencer.actualSoundingLaunch(), nowUs, reason, service);
        _internalTiming = false;
        _internalTickDiscardReason = discardReason;
        if (resetExternal) {
            _external.reset();
        }
    }

    auto finishPass(std::uint32_t nowUs) -> void {
        if (_dispatcher.capacityExceeded()) {
            stopForDeliveryCapacity();
        }
        _dispatcher.finishPass(nowUs);
        handleLifecycleOutcome(nowUs);
    }

    auto stopForDeliveryCapacity() noexcept -> void {
        addOne(_diagnostics.deliveryCapacitySafetyStops);
        stop(InternalTickDiscardReason::Stop, true, 0, true, InvalidationReason::DeliveryCapacity);
        _dispatcher.clearCapacityExceeded();
    }

    auto handleLifecycleOutcome(std::uint32_t nowUs) -> void {
        const auto outcome = _dispatcher.takeLifecycleOutcome();
        if (outcome == DeliveryLifecycleOutcome::None) {
            return;
        }
        if (outcome == DeliveryLifecycleOutcome::Disconnected) {
            const bool remoteUnknown =
                _sequencer.actualSoundingNote().has_value() || _dispatcher.hasTerminalNoteOff();
            (void)_dispatcher.abandonPending();
            _dispatcher.recordScheduledRemoval(_queue.clear(), DeliveryRemovalReason::Disconnected);
            (void)_sequencer.stop();
            if (remoteUnknown) {
                _sequencer.abandonRemoteNoteState();
            }
            _transport.stop();
            _external.reset();
            _internalTiming = false;
            _internalTickDiscardReason = InternalTickDiscardReason::Stop;
            _requiresExplicitStart = true;
            return;
        }
        addOne(_diagnostics.retryWindowSafetyStops);
        stop(InternalTickDiscardReason::Stop, true, nowUs, false,
             InvalidationReason::RetryWindowExceeded);
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
    bool _requiresExplicitStart = false;
    InternalTickDiscardReason _internalTickDiscardReason = InternalTickDiscardReason::Stop;
    std::uint32_t _lastServiceAtUs = 0;
    bool _haveServiceTimestamp = false;
    static constexpr std::uint8_t MAX_INTERNAL_TICKS_PER_PASS = 4;
};

} // namespace SwingMetro
