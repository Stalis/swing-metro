#pragma once

#include "midi_realtime_event.h"
#include "timestamp.h"
#include "transport_tick.h"

#include <cstdint>

namespace SwingMetro {

enum class ExternalMidiClockStatus : std::uint8_t { Waiting, Locked, Lost };

struct ExternalMidiClockResult {
    bool reset = false;
    bool started = false;
    bool stopped = false;
    bool tick = false;
    TransportTickRecord tickRecord{};

    // Legacy main.cpp consumes six incoming clocks as one sequencer step until stage 6.
    bool advanceStep = false;
};

class ExternalMidiClock {
  public:
    static constexpr std::uint32_t CLOCK_LOSS_TIMEOUT_US = 250'000;
    static constexpr std::uint8_t TICKS_PER_STEP = 6;
    static constexpr std::uint32_t MICROSECONDS_PER_MINUTE = 60'000'000;
    static constexpr std::uint8_t PPQN = 24;
    static constexpr std::uint32_t MIN_PERIOD_US = MICROSECONDS_PER_MINUTE / (240U * PPQN);
    static constexpr std::uint32_t MAX_PERIOD_US = MICROSECONDS_PER_MINUTE / (40U * PPQN);

    auto reset() noexcept -> void {
        _status = ExternalMidiClockStatus::Waiting;
        _running = false;
        _haveClock = false;
        _legacyPhase = 0;
        _periodUs = 0;
    }

    [[nodiscard]] auto status() const noexcept -> ExternalMidiClockStatus { return _status; }
    [[nodiscard]] auto bpm() const noexcept -> std::uint8_t {
        return static_cast<std::uint8_t>(bpmMilli() / 1000U);
    }
    [[nodiscard]] auto bpmMilli() const noexcept -> std::uint32_t {
        return _periodUs == 0 ? 0
                              : static_cast<std::uint32_t>(
                                    (static_cast<std::uint64_t>(MICROSECONDS_PER_MINUTE) * 1000U) /
                                    (_periodUs * PPQN));
    }
    [[nodiscard]] auto periodUs() const noexcept -> std::uint32_t { return _periodUs; }
    [[nodiscard]] auto running() const noexcept -> bool { return _running; }

    auto handle(const MidiRealtimeEvent& event) noexcept -> ExternalMidiClockResult {
        switch (event.type) {
        case MidiRealtimeEventType::Start:
            _legacyPhase = 0;
            _running = true;
            return {.reset = true, .started = true, .advanceStep = true};
        case MidiRealtimeEventType::Continue:
            _running = true;
            return {.started = true};
        case MidiRealtimeEventType::Stop:
            _running = false;
            return {.stopped = true};
        case MidiRealtimeEventType::Clock:
            return handleClock(event.timestampUs);
        }
        return {};
    }

    auto update(std::uint32_t nowUs) noexcept -> ExternalMidiClockResult {
        if (_haveClock && timestampReached(nowUs, _lastClockAtUs + CLOCK_LOSS_TIMEOUT_US) &&
            _status != ExternalMidiClockStatus::Lost) {
            _status = ExternalMidiClockStatus::Lost;
            const bool wasRunning = _running;
            _running = false;
            _haveClock = false;
            _legacyPhase = 0;
            return {.stopped = wasRunning};
        }
        return {};
    }

  private:
    auto handleClock(std::uint32_t timestampUs) noexcept -> ExternalMidiClockResult {
        const auto sampleUs = timestampUs - _lastClockAtUs;
        if (_haveClock && sampleUs >= MIN_PERIOD_US && sampleUs <= MAX_PERIOD_US &&
            _periodUs != 0) {
            const auto delta = static_cast<std::int32_t>(sampleUs - _periodUs);
            _periodUs =
                static_cast<std::uint32_t>(static_cast<std::int32_t>(_periodUs) + delta / 4);
        } else if (_haveClock && sampleUs >= MIN_PERIOD_US && sampleUs <= MAX_PERIOD_US) {
            _periodUs = sampleUs;
        } else {
            _haveClock = true;
        }
        _lastClockAtUs = timestampUs;
        _status = ExternalMidiClockStatus::Locked;

        ExternalMidiClockResult result;
        if (_running) {
            if (_periodUs != 0) {
                result.tick = true;
                result.tickRecord = {timestampUs, _periodUs};
            }
            ++_legacyPhase;
            if (_legacyPhase == TICKS_PER_STEP) {
                _legacyPhase = 0;
                result.advanceStep = true;
            }
        }
        return result;
    }

    ExternalMidiClockStatus _status = ExternalMidiClockStatus::Waiting;
    std::uint32_t _lastClockAtUs = 0;
    std::uint32_t _periodUs = 0;
    std::uint8_t _legacyPhase = 0;
    bool _haveClock = false;
    bool _running = false;
};

} // namespace SwingMetro
