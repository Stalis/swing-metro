#pragma once

#include "midi_event_queue.h"

#include <cstddef>
#include <cstdint>

namespace SwingMetro {

enum class SendResult : std::uint8_t {
    Accepted,
    RetryLater,
    Disconnected,
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

} // namespace SwingMetro
