#pragma once

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

struct InternalTickDiagnostics {
    std::uint32_t alarmCallbackInvocations = 0;
    std::uint32_t synchronousStartPublicationAttempts = 0;
    std::uint32_t successfulPublications = 0;
    std::uint32_t failedPublications = 0;
    std::uint32_t stopDiscards = 0;
    std::uint32_t modeSwitchDiscards = 0;
    std::uint32_t storageDiscards = 0;
    std::uint32_t alarmArmFailures = 0;
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
        _active = true;
        setBpm(bpm);
        _haveCallbackTimestamp = false;
        addDiscards(discardReason, _ticks.discard());
        const auto periodUs = nextPeriodUs();
        addOne(_diagnostics.synchronousStartPublicationAttempts);
        publish({timestampUs, periodUs});
        endDiagnosticsUpdate();
        return periodUs;
    }

    auto stop(InternalTickDiscardReason discardReason = InternalTickDiscardReason::Stop) noexcept
        -> void {
        beginDiagnosticsUpdate();
        _active = false;
        addDiscards(discardReason, _ticks.discard());
        endDiagnosticsUpdate();
    }

    auto setBpm(std::uint8_t bpm) noexcept -> void {
        _bpm = clampBpm(bpm);
        _fractionalUs = 0;
    }

    [[nodiscard]] auto onAlarm(std::uint32_t scheduledDeadlineUs,
                               std::uint32_t actualCallbackAtUs) noexcept -> std::uint32_t {
        beginDiagnosticsUpdate();
        addOne(_diagnostics.alarmCallbackInvocations);
        updateCallbackTiming(scheduledDeadlineUs, actualCallbackAtUs);
        if (!_active) {
            endDiagnosticsUpdate();
            return 0;
        }
        const auto periodUs = nextPeriodUs();
        publish({actualCallbackAtUs, periodUs});
        endDiagnosticsUpdate();
        return periodUs;
    }

    // Called by the Pico driver while holding its producer critical section.
    auto onAlarmArmFailure() noexcept -> void {
        beginDiagnosticsUpdate();
        addOne(_diagnostics.alarmArmFailures);
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

    auto updateCallbackTiming(std::uint32_t scheduledDeadlineUs,
                              std::uint32_t actualCallbackAtUs) noexcept -> void {
        if (_haveCallbackTimestamp && timestampReached(actualCallbackAtUs, _lastCallbackAtUs)) {
            updateMaximum(_diagnostics.maxActualCallbackIntervalUs,
                          actualCallbackAtUs - _lastCallbackAtUs);
        }
        _lastCallbackAtUs = actualCallbackAtUs;
        _haveCallbackTimestamp = true;
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
                _diagnostics.maxActualCallbackIntervalUs.load(std::memory_order_relaxed),
                _diagnostics.maxCallbackLatenessUs.load(std::memory_order_relaxed)};
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
    std::uint8_t _bpm = MIN_BPM;
    std::uint32_t _fractionalUs = 0;
    std::uint32_t _lastCallbackAtUs = 0;
    bool _haveCallbackTimestamp = false;
};

} // namespace SwingMetro
