#pragma once

#include "engine/midi_realtime_event.h"
#include "engine/midi_usb_packet.h"
#include "engine/transport_controller.h"

#include <Adafruit_TinyUSB.h>

#include <cstdint>

namespace SwingMetro {

class UsbMidiPacketSink final : public MidiPacketSink {
  public:
    explicit UsbMidiPacketSink(Adafruit_USBD_MIDI& midi) noexcept : _midi{midi} {}

    auto send(const MidiUsbPacket& packet) -> void override {
        (void)_midi.writePacket(packet.data());
    }

  private:
    Adafruit_USBD_MIDI& _midi;
};

class UsbMidiRealtimeReceiver final {
  public:
    explicit UsbMidiRealtimeReceiver(Adafruit_USBD_MIDI& midi) noexcept : _midi{midi} {}

    template <typename HandleEvent>
    auto poll(HandleEvent handleEvent) -> void {
        std::uint8_t packet[4];
        while (_midi.readPacket(packet)) {
            const auto event = midiRealtimeEventFromUsbPacket(packet, micros());
            if (event.has_value()) {
                handleEvent(*event);
            }
        }
    }

  private:
    Adafruit_USBD_MIDI& _midi;
};

} // namespace SwingMetro
