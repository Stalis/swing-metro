#pragma once

#include "transport_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

enum class FaultScenario : std::uint8_t {
    Baseline,
    RetryFirstClock,
    SustainedBackpressure,
    DeterministicDisconnect,
};

class FaultMidiMessageSink final : public MidiMessageSink {
  public:
    static constexpr std::size_t ATTEMPT_CAPACITY = 64;

    explicit FaultMidiMessageSink(MidiMessageSink& delegate) noexcept : _delegate{delegate} {}

    auto select(FaultScenario scenario) noexcept -> void {
        _scenario = scenario;
        reset();
    }

    [[nodiscard]] auto scenario() const noexcept -> FaultScenario { return _scenario; }

    auto reset() noexcept -> void {
        _attemptCount = 0;
        _clockAttempts = 0;
    }

    auto send(const MidiDeliveryAttempt& attempt) -> SendResult override {
        if (_attemptCount < attempts.size()) {
            attempts[_attemptCount] = attempt;
            ++_attemptCount;
        }
        if (attempt.message.messageClass() != MidiMessageClass::Clock) {
            return _delegate.send(attempt);
        }
        const auto clockAttempt = _clockAttempts++;
        switch (_scenario) {
        case FaultScenario::Baseline:
            return _delegate.send(attempt);
        case FaultScenario::RetryFirstClock:
            return clockAttempt == 0 ? SendResult::RetryLater : _delegate.send(attempt);
        case FaultScenario::SustainedBackpressure:
            return SendResult::RetryLater;
        case FaultScenario::DeterministicDisconnect:
            return clockAttempt == 0 ? SendResult::Disconnected : _delegate.send(attempt);
        }
        return _delegate.send(attempt);
    }

    [[nodiscard]] auto attemptCount() const noexcept -> std::size_t { return _attemptCount; }

    std::array<MidiDeliveryAttempt, ATTEMPT_CAPACITY> attempts{};

  private:
    MidiMessageSink& _delegate;
    FaultScenario _scenario = FaultScenario::Baseline;
    std::size_t _attemptCount = 0;
    std::uint32_t _clockAttempts = 0;
};

} // namespace SwingMetro
