#pragma once

#include "external_midi_clock.h"
#include "internal_tick_source.h"
#include "midi_clock_mode.h"
#include "midi_dispatcher.h"
#include "midi_event_queue.h"
#include "midi_message_sink.h"
#include "midi_pending_delivery_queue.h"
#include "sequencer.h"
#include "stage5_instrumentation.h"
#include "timestamp.h"
#include "transport.h"
#include "transport_diagnostics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace SwingMetro {

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
