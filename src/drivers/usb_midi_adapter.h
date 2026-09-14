#pragma once

#include "engine/midi_realtime_event.h"
#include "engine/midi_usb_packet.h"
#include "engine/transport_controller.h"

#include <Adafruit_TinyUSB.h>

#include <cstdint>

namespace SwingMetro {

class UsbMidiPacketSink final : public MidiPacketSink {
  public:
    explicit UsbMidiPacketSink(Adafruit_USBD_MIDI& midi) noexcept : midi_{midi} {}

    auto send(const MidiUsbPacket& packet) -> void override {
        (void)midi_.writePacket(packet.data());
    }

  private:
    Adafruit_USBD_MIDI& midi_;
};

class UsbMidiRealtimeReceiver final {
  public:
    explicit UsbMidiRealtimeReceiver(Adafruit_USBD_MIDI& midi) noexcept : midi_{midi} {}

    template <typename HandleEvent>
    auto poll(HandleEvent handleEvent) -> void {
        std::uint8_t packet[4];
        while (midi_.readPacket(packet)) {
            const auto event = midiRealtimeEventFromUsbPacket(packet, micros());
            if (event.has_value()) {
                handleEvent(*event);
            }
        }
    }

  private:
    Adafruit_USBD_MIDI& midi_;
};

} // namespace SwingMetro
