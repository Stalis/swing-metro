#pragma once

#include "midi_realtime_event.h"

#include <cstdint>

namespace SwingMetro {

enum class ExternalMidiClockStatus : std::uint8_t { Waiting, Locked, Lost };

struct ExternalMidiClockResult {
    bool reset = false;
    bool started = false;
    bool stopped = false;
    bool advanceStep = false;
};

class ExternalMidiClock {
  public:
    static constexpr std::uint32_t kClockLossTimeoutUs = 250'000;
    static constexpr std::uint8_t kTicksPerStep = 6;

    auto reset() noexcept -> void {
        status_ = ExternalMidiClockStatus::Waiting;
        running_ = false;
        haveClock_ = false;
        phase_ = 0;
        periodUs_ = 0;
    }

    [[nodiscard]] auto status() const noexcept -> ExternalMidiClockStatus { return status_; }
    [[nodiscard]] auto bpm() const noexcept -> std::uint8_t {
        return periodUs_ == 0 ? 0 : static_cast<std::uint8_t>(60'000'000U / (periodUs_ * 24U));
    }

    auto handle(const MidiRealtimeEvent& event) noexcept -> ExternalMidiClockResult {
        switch (event.type) {
        case MidiRealtimeEventType::Start:
            phase_ = 0;
            running_ = true;
            return {.reset = true, .started = true, .advanceStep = true};
        case MidiRealtimeEventType::Continue:
            running_ = true;
            return {.started = true};
        case MidiRealtimeEventType::Stop:
            running_ = false;
            return {.stopped = true};
        case MidiRealtimeEventType::Clock:
            return handleClock(event.timestampUs);
        }
        return {};
    }

    auto update(std::uint32_t nowUs) noexcept -> ExternalMidiClockResult {
        if (haveClock_ &&
            static_cast<std::int32_t>(nowUs - lastClockAtUs_) >=
                static_cast<std::int32_t>(kClockLossTimeoutUs) &&
            status_ != ExternalMidiClockStatus::Lost) {
            status_ = ExternalMidiClockStatus::Lost;
            const bool wasRunning = running_;
            running_ = false;
            haveClock_ = false;
            phase_ = 0;
            periodUs_ = 0;
            return {.stopped = wasRunning};
        }
        return {};
    }

  private:
    auto handleClock(std::uint32_t timestampUs) noexcept -> ExternalMidiClockResult {
        if (haveClock_ && periodUs_ != 0) {
            const auto sampleUs = timestampUs - lastClockAtUs_;
            const auto delta = static_cast<std::int32_t>(sampleUs - periodUs_);
            periodUs_ =
                static_cast<std::uint32_t>(static_cast<std::int32_t>(periodUs_) + delta / 4);
        } else if (haveClock_) {
            periodUs_ = timestampUs - lastClockAtUs_;
        } else {
            haveClock_ = true;
        }
        lastClockAtUs_ = timestampUs;
        status_ = ExternalMidiClockStatus::Locked;

        if (!running_) {
            return {};
        }
        ++phase_;
        if (phase_ == kTicksPerStep) {
            phase_ = 0;
            return {.advanceStep = true};
        }
        return {};
    }

    ExternalMidiClockStatus status_ = ExternalMidiClockStatus::Waiting;
    std::uint32_t lastClockAtUs_ = 0;
    std::uint32_t periodUs_ = 0;
    std::uint8_t phase_ = 0;
    bool haveClock_ = false;
    bool running_ = false;
};

} // namespace SwingMetro
