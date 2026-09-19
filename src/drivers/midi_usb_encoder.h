#pragma once

#include "engine/midi_message.h"

#include <array>
#include <cstdint>

namespace SwingMetro {

using MidiUsbPacket = std::array<std::uint8_t, 4>;

[[nodiscard]] constexpr auto encodeMidiUsbPacket(const MidiMessage& message) noexcept
    -> MidiUsbPacket {
    switch (message.type()) {
    case MidiMessageType::NoteOn:
        return {0x09, static_cast<std::uint8_t>(0x90 | message.channel()), message.note(),
                message.velocity()};
    case MidiMessageType::NoteOff:
        return {0x08, static_cast<std::uint8_t>(0x80 | message.channel()), message.note(), 0};
    case MidiMessageType::Start:
        return {0x0F, 0xFA, 0, 0};
    case MidiMessageType::Continue:
        return {0x0F, 0xFB, 0, 0};
    case MidiMessageType::Stop:
        return {0x0F, 0xFC, 0, 0};
    case MidiMessageType::Clock:
        return {0x0F, 0xF8, 0, 0};
    }
    return {0x0F, 0xF8, 0, 0};
}

} // namespace SwingMetro
