#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "timestamp.h"
#include "transport_tick.h"

namespace SwingMetro {

using InternalTickRecord = TransportTickRecord;

template <std::size_t Capacity = 16>
using InternalTickStore = TransportTickStore<Capacity>;

enum class InternalTickDiscardReason : std::uint8_t {
    Stop,
    ModeSwitch,
    Storage,
};

struct InternalTickAlarmRequest {
    std::uint32_t generation = 0;
    std::uint32_t deadlineUs = 0;

    [[nodiscard]] constexpr auto valid() const noexcept -> bool { return generation != 0; }
};

struct InternalTickDiagnostics {
    std::uint32_t alarmCallbackInvocations = 0;
    std::uint32_t synchronousStartPublicationAttempts = 0;
    std::uint32_t successfulPublications = 0;
    std::uint32_t failedPublications = 0;
    std::uint32_t stopDiscards = 0;
    std::uint32_t modeSwitchDiscards = 0;
    std::uint32_t storageDiscards = 0;
    std::uint32_t alarmArmFailures = 0;
    std::uint32_t staleAlarmCallbacks = 0;
    std::uint32_t staleAlarmArmFailures = 0;
    std::uint32_t missedScheduledTargets = 0;
    std::uint32_t outOfHorizonAlarmCallbacks = 0;
    std::uint32_t maxActualCallbackIntervalUs = 0;
    std::uint32_t maxCallbackLatenessUs = 0;
};

class InternalTickSource {
  public:
    static constexpr std::uint8_t MIN_BPM = 40;
    static constexpr std::uint8_t MAX_BPM = 240;
    static constexpr std::uint32_t MICROSECONDS_PER_MINUTE = 60'000'000;
    static constexpr std::uint8_t PPQN = 24;

    [[nodiscard]] static constexpr auto clampBpm(std::uint8_t bpm) noexcept -> std::uint8_t {
        return bpm < MIN_BPM ? MIN_BPM : (bpm > MAX_BPM ? MAX_BPM : bpm);
    }

    [[nodiscard]] static constexpr auto periodForBpm(std::uint8_t bpm) noexcept -> std::uint32_t {
        return MICROSECONDS_PER_MINUTE / (static_cast<std::uint32_t>(clampBpm(bpm)) * PPQN);
    }

    auto start(std::uint32_t timestampUs, std::uint8_t bpm,
               InternalTickDiscardReason discardReason = InternalTickDiscardReason::Stop) noexcept
        -> std::uint32_t {
        beginDiagnosticsUpdate();
        nextGeneration();
        _active = true;
        setBpm(bpm);
        _haveCallbackTimestamp = false;
        addDiscards(discardReason, _ticks.discard());
        const auto periodUs = nextPeriodUs();
        _nextDeadlineUs = timestampUs + periodUs;
        addOne(_diagnostics.synchronousStartPublicationAttempts);
        publish({timestampUs, periodUs});
        endDiagnosticsUpdate();
        return periodUs;
    }

    auto stop(InternalTickDiscardReason discardReason = InternalTickDiscardReason::Stop) noexcept
        -> void {
        beginDiagnosticsUpdate();
        _active = false;
        nextGeneration();
        addDiscards(discardReason, _ticks.discard());
        endDiagnosticsUpdate();
    }

    auto setBpm(std::uint8_t bpm) noexcept -> void {
        _bpm = clampBpm(bpm);
        _fractionalUs = 0;
    }

    [[nodiscard]] auto setBpmAt(std::uint8_t bpm, std::uint32_t appliedAtUs) noexcept
        -> std::uint32_t {
        beginDiagnosticsUpdate();
        setBpm(bpm);
        if (_active) {
            nextGeneration();
            const auto periodUs = nextPeriodUs();
            _nextDeadlineUs = appliedAtUs + periodUs;
            endDiagnosticsUpdate();
            return periodUs;
        }
        endDiagnosticsUpdate();
        return 0;
    }

    [[nodiscard]] auto alarmRequest() const noexcept -> InternalTickAlarmRequest {
        return _active ? InternalTickAlarmRequest{_generation, _nextDeadlineUs}
                       : InternalTickAlarmRequest{};
    }

    [[nodiscard]] auto onAlarm(InternalTickAlarmRequest request,
                               std::uint32_t actualCallbackAtUs) noexcept -> std::uint32_t {
        beginDiagnosticsUpdate();
        addOne(_diagnostics.alarmCallbackInvocations);
        updateCallbackInterval(actualCallbackAtUs);
        if (!_active || request.generation != _generation ||
            request.deadlineUs != _nextDeadlineUs) {
            addOne(_diagnostics.staleAlarmCallbacks);
            endDiagnosticsUpdate();
            return 0;
        }
        if (!timestampReached(actualCallbackAtUs, request.deadlineUs)) {
            // An early or >= 2^31-us callback cannot be ordered against this grid.
            addOne(_diagnostics.outOfHorizonAlarmCallbacks);
            _active = false;
            nextGeneration();
            endDiagnosticsUpdate();
            return 0;
        }
        updateCallbackLateness(request.deadlineUs, actualCallbackAtUs);
        const auto periodUs = nextPeriodUs();
        publish({request.deadlineUs, periodUs});
        _nextDeadlineUs += periodUs;

        const auto elapsedUs = actualCallbackAtUs - _nextDeadlineUs;
        if (timestampReached(actualCallbackAtUs, _nextDeadlineUs)) {
            const auto skipped = duePeriods(elapsedUs);
            addCount(_diagnostics.missedScheduledTargets, skipped);
            advancePeriods(skipped);
        }
        endDiagnosticsUpdate();
        return periodUs;
    }

    // Called by the Pico driver while holding its producer critical section.
    auto onAlarmArmFailure(InternalTickAlarmRequest request) noexcept -> void {
        beginDiagnosticsUpdate();
        addOne(_diagnostics.alarmArmFailures);
        if (_active && request.generation == _generation && request.deadlineUs == _nextDeadlineUs) {
            _active = false;
            nextGeneration();
        } else {
            addOne(_diagnostics.staleAlarmArmFailures);
        }
        endDiagnosticsUpdate();
    }

    [[nodiscard]] auto ticks() noexcept -> InternalTickStore<>& { return _ticks; }

    [[nodiscard]] auto diagnostics() const noexcept -> InternalTickDiagnostics {
        InternalTickDiagnostics snapshot;
        std::uint32_t before = 0;
        do {
            before = _diagnosticsVersion.load(std::memory_order_acquire);
            if ((before & 1U) != 0U) {
                continue;
            }
            snapshot = loadDiagnostics();
            std::atomic_thread_fence(std::memory_order_acquire);
        } while (before != _diagnosticsVersion.load(std::memory_order_relaxed));
        return snapshot;
    }

  private:
    struct AtomicDiagnostics {
        std::atomic<std::uint32_t> alarmCallbackInvocations{0};
        std::atomic<std::uint32_t> synchronousStartPublicationAttempts{0};
        std::atomic<std::uint32_t> successfulPublications{0};
        std::atomic<std::uint32_t> failedPublications{0};
        std::atomic<std::uint32_t> stopDiscards{0};
        std::atomic<std::uint32_t> modeSwitchDiscards{0};
        std::atomic<std::uint32_t> storageDiscards{0};
        std::atomic<std::uint32_t> alarmArmFailures{0};
        std::atomic<std::uint32_t> staleAlarmCallbacks{0};
        std::atomic<std::uint32_t> staleAlarmArmFailures{0};
        std::atomic<std::uint32_t> missedScheduledTargets{0};
        std::atomic<std::uint32_t> outOfHorizonAlarmCallbacks{0};
        std::atomic<std::uint32_t> maxActualCallbackIntervalUs{0};
        std::atomic<std::uint32_t> maxCallbackLatenessUs{0};
    };

    static auto addOne(std::atomic<std::uint32_t>& counter) noexcept -> void {
        const auto value = counter.load(std::memory_order_relaxed);
        if (value != UINT32_MAX) {
            counter.store(value + 1, std::memory_order_relaxed);
        }
    }

    static auto addCount(std::atomic<std::uint32_t>& counter, std::size_t count) noexcept -> void {
        const auto value = counter.load(std::memory_order_relaxed);
        const auto available = static_cast<std::size_t>(UINT32_MAX - value);
        counter.store(value + static_cast<std::uint32_t>(count > available ? available : count),
                      std::memory_order_relaxed);
    }

    static auto updateMaximum(std::atomic<std::uint32_t>& maximum, std::uint32_t value) noexcept
        -> void {
        if (value > maximum.load(std::memory_order_relaxed)) {
            maximum.store(value, std::memory_order_relaxed);
        }
    }

    auto beginDiagnosticsUpdate() noexcept -> void {
        _diagnosticsVersion.fetch_add(1, std::memory_order_acq_rel);
    }

    auto endDiagnosticsUpdate() noexcept -> void {
        _diagnosticsVersion.fetch_add(1, std::memory_order_release);
    }

    auto publish(TransportTickRecord record) noexcept -> void {
        if (_ticks.publish(record)) {
            addOne(_diagnostics.successfulPublications);
        } else {
            addOne(_diagnostics.failedPublications);
        }
    }

    auto addDiscards(InternalTickDiscardReason reason, std::size_t count) noexcept -> void {
        switch (reason) {
        case InternalTickDiscardReason::Stop:
            addCount(_diagnostics.stopDiscards, count);
            break;
        case InternalTickDiscardReason::ModeSwitch:
            addCount(_diagnostics.modeSwitchDiscards, count);
            break;
        case InternalTickDiscardReason::Storage:
            addCount(_diagnostics.storageDiscards, count);
            break;
        }
    }

    auto updateCallbackInterval(std::uint32_t actualCallbackAtUs) noexcept -> void {
        if (_haveCallbackTimestamp && timestampReached(actualCallbackAtUs, _lastCallbackAtUs)) {
            updateMaximum(_diagnostics.maxActualCallbackIntervalUs,
                          actualCallbackAtUs - _lastCallbackAtUs);
        }
        _lastCallbackAtUs = actualCallbackAtUs;
        _haveCallbackTimestamp = true;
    }

    auto updateCallbackLateness(std::uint32_t scheduledDeadlineUs,
                                std::uint32_t actualCallbackAtUs) noexcept -> void {
        if (timestampReached(actualCallbackAtUs, scheduledDeadlineUs)) {
            updateMaximum(_diagnostics.maxCallbackLatenessUs,
                          actualCallbackAtUs - scheduledDeadlineUs);
        }
    }

    [[nodiscard]] auto loadDiagnostics() const noexcept -> InternalTickDiagnostics {
        return {_diagnostics.alarmCallbackInvocations.load(std::memory_order_relaxed),
                _diagnostics.synchronousStartPublicationAttempts.load(std::memory_order_relaxed),
                _diagnostics.successfulPublications.load(std::memory_order_relaxed),
                _diagnostics.failedPublications.load(std::memory_order_relaxed),
                _diagnostics.stopDiscards.load(std::memory_order_relaxed),
                _diagnostics.modeSwitchDiscards.load(std::memory_order_relaxed),
                _diagnostics.storageDiscards.load(std::memory_order_relaxed),
                _diagnostics.alarmArmFailures.load(std::memory_order_relaxed),
                _diagnostics.staleAlarmCallbacks.load(std::memory_order_relaxed),
                _diagnostics.staleAlarmArmFailures.load(std::memory_order_relaxed),
                _diagnostics.missedScheduledTargets.load(std::memory_order_relaxed),
                _diagnostics.outOfHorizonAlarmCallbacks.load(std::memory_order_relaxed),
                _diagnostics.maxActualCallbackIntervalUs.load(std::memory_order_relaxed),
                _diagnostics.maxCallbackLatenessUs.load(std::memory_order_relaxed)};
    }

    auto nextGeneration() noexcept -> void {
        ++_generation;
        if (_generation == 0) {
            ++_generation;
        }
    }

    [[nodiscard]] auto duePeriods(std::uint32_t elapsedUs) const noexcept -> std::uint32_t {
        // _nextDeadlineUs is itself due. Count it plus every following rational-grid
        // target at or before actualCallbackAtUs, including exact equality.
        const auto denominator = static_cast<std::uint64_t>(_bpm) * PPQN;
        const auto numerator =
            (static_cast<std::uint64_t>(elapsedUs) + 1U) * denominator - 1U - _fractionalUs;
        return static_cast<std::uint32_t>(numerator / MICROSECONDS_PER_MINUTE) + 1U;
    }

    auto advancePeriods(std::uint32_t count) noexcept -> void {
        if (count == 0) {
            return;
        }
        const auto denominator = static_cast<std::uint64_t>(_bpm) * PPQN;
        // Advance count rational periods in one bounded operation while preserving
        // the same residue sequence as repeated nextPeriodUs() calls.
        const auto total =
            static_cast<std::uint64_t>(count) * MICROSECONDS_PER_MINUTE + _fractionalUs;
        _nextDeadlineUs += static_cast<std::uint32_t>(total / denominator);
        _fractionalUs = static_cast<std::uint32_t>(total % denominator);
    }

    [[nodiscard]] auto nextPeriodUs() noexcept -> std::uint32_t {
        const auto denominator = static_cast<std::uint32_t>(_bpm) * PPQN;
        const auto remainder = MICROSECONDS_PER_MINUTE % denominator;
        const auto periodUs =
            MICROSECONDS_PER_MINUTE / denominator + (_fractionalUs + remainder >= denominator);
        _fractionalUs = (_fractionalUs + remainder) % denominator;
        return periodUs;
    }

    InternalTickStore<> _ticks;
    std::atomic<std::uint32_t> _diagnosticsVersion{0};
    AtomicDiagnostics _diagnostics;
    bool _active = false;
    std::uint32_t _generation = 0;
    std::uint8_t _bpm = MIN_BPM;
    std::uint32_t _fractionalUs = 0;
    std::uint32_t _nextDeadlineUs = 0;
    std::uint32_t _lastCallbackAtUs = 0;
    bool _haveCallbackTimestamp = false;
};

} // namespace SwingMetro
