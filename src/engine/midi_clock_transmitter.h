#pragma once

#include "midi_clock_mode.h"

#include <cstdint>

namespace SwingMetro {

class MidiRealTimeSink {
  public:
    virtual ~MidiRealTimeSink() = default;
    virtual auto send(std::uint8_t status) -> void = 0;
};

struct MidiClockTickResult {
    bool transportStarted = false;
    bool transportStopped = false;
};

class MidiClockTransmitter {
  public:
    static constexpr std::uint8_t kClockStatus = 0xF8;
    static constexpr std::uint8_t kStartStatus = 0xFA;
    static constexpr std::uint8_t kStopStatus = 0xFC;
    static constexpr std::uint8_t kPpqn = 24;
    static constexpr std::uint8_t kMaxCatchUpPulses = 4;

    auto transition(std::uint32_t nowUs, std::uint8_t bpm, MidiClockMode mode, bool running,
                    MidiRealTimeSink& sink) noexcept -> MidiClockTickResult {
        MidiClockTickResult result;
        if (observedTransport_) {
            result.transportStarted = !running_ && running;
            result.transportStopped = running_ && !running;
        } else {
            observedTransport_ = true;
        }
        running_ = running;

        const bool shouldRun = mode == MidiClockMode::Internal && running;
        if (shouldRun && !active_) {
            active_ = true;
            setPhase(nowUs, bpm);
            sink.send(kStartStatus);
        } else if (!shouldRun && active_) {
            active_ = false;
            sink.send(kStopStatus);
        }

        if (!active_) {
            return result;
        }

        updateBpm(bpm);
        return result;
    }

    auto emitDueClocks(std::uint32_t nowUs, MidiRealTimeSink& sink) noexcept -> void {
        if (!active_) {
            return;
        }

        std::uint8_t sent = 0;
        while (sent < kMaxCatchUpPulses && nowUs - lastClockAtUs_ >= nextPeriodUs()) {
            const auto periodUs = nextPeriodUs();
            lastClockAtUs_ += periodUs;
            advanceFraction();
            sink.send(kClockStatus);
            ++sent;
        }

        if (sent == kMaxCatchUpPulses && nowUs - lastClockAtUs_ >= nextPeriodUs()) {
            // ponytail: drop residual clock debt; raise the limit only if a measured loop stall
            // needs it.
            setPhase(nowUs, bpm_);
        }
    }

    auto tick(std::uint32_t nowUs, std::uint8_t bpm, MidiClockMode mode, bool running,
              MidiRealTimeSink& sink) noexcept -> MidiClockTickResult {
        const auto result = transition(nowUs, bpm, mode, running, sink);
        emitDueClocks(nowUs, sink);
        return result;
    }

  private:
    static constexpr std::uint8_t kMinBpm = 40;
    static constexpr std::uint8_t kMaxBpm = 240;
    static constexpr std::uint32_t kMicrosecondsPerMinute = 60'000'000;

    [[nodiscard]] static constexpr auto clampBpm(std::uint8_t bpm) noexcept -> std::uint8_t {
        return bpm < kMinBpm ? kMinBpm : (bpm > kMaxBpm ? kMaxBpm : bpm);
    }

    [[nodiscard]] auto denominator() const noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(bpm_) * kPpqn;
    }

    [[nodiscard]] auto nextPeriodUs() const noexcept -> std::uint32_t {
        return kMicrosecondsPerMinute / denominator() +
               (fractionalUs_ + kMicrosecondsPerMinute % denominator() >= denominator());
    }

    auto advanceFraction() noexcept -> void {
        fractionalUs_ += kMicrosecondsPerMinute % denominator();
        if (fractionalUs_ >= denominator()) {
            fractionalUs_ -= denominator();
        }
    }

    auto updateBpm(std::uint8_t bpm) noexcept -> void {
        const auto nextBpm = clampBpm(bpm);
        if (nextBpm != bpm_) {
            // A BPM change keeps the last pulse timestamp; its next deadline uses the new rate.
            bpm_ = nextBpm;
            fractionalUs_ = 0;
        }
    }

    auto setPhase(std::uint32_t nowUs, std::uint8_t bpm) noexcept -> void {
        lastClockAtUs_ = nowUs;
        bpm_ = clampBpm(bpm);
        fractionalUs_ = 0;
    }

    bool observedTransport_ = false;
    bool running_ = false;
    bool active_ = false;
    std::uint8_t bpm_ = kMinBpm;
    std::uint32_t lastClockAtUs_ = 0;
    std::uint32_t fractionalUs_ = 0;
};

} // namespace SwingMetro
