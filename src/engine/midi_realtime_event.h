#pragma once

#include <cstdint>
#include <optional>

namespace SwingMetro {

enum class MidiRealtimeEventType : std::uint8_t { Clock, Start, Continue, Stop };

struct MidiRealtimeEvent {
    MidiRealtimeEventType type;
    std::uint32_t timestampUs;
};

[[nodiscard]] constexpr auto midiRealtimeEventFromUsbPacket(const std::uint8_t packet[4],
                                                            std::uint32_t timestampUs) noexcept
    -> std::optional<MidiRealtimeEvent> {
    if (packet[0] != 0x0F) {
        return std::nullopt;
    }

    switch (packet[1]) {
    case 0xF8:
        return MidiRealtimeEvent{MidiRealtimeEventType::Clock, timestampUs};
    case 0xFA:
        return MidiRealtimeEvent{MidiRealtimeEventType::Start, timestampUs};
    case 0xFB:
        return MidiRealtimeEvent{MidiRealtimeEventType::Continue, timestampUs};
    case 0xFC:
        return MidiRealtimeEvent{MidiRealtimeEventType::Stop, timestampUs};
    default:
        return std::nullopt;
    }
}

} // namespace SwingMetro
