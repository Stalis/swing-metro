#pragma once

#include <array>
#include <cstdint>

namespace SwingMetro {

using MidiUsbPacket = std::array<std::uint8_t, 4>;

[[nodiscard]] constexpr auto usbMidiRealTimePacket(std::uint8_t status) noexcept -> MidiUsbPacket {
    return {0x0F, status, 0x00, 0x00};
}

} // namespace SwingMetro
