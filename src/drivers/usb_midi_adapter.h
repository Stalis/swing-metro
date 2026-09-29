#pragma once

#include "drivers/midi_usb_encoder.h"
#include "engine/midi_message_sink.h"
#include "engine/midi_realtime_event.h"

#include <Adafruit_TinyUSB.h>

#include <cstdint>

namespace SwingMetro {

class UsbMidiMessageSink final : public MidiMessageSink {
  public:
    explicit UsbMidiMessageSink(Adafruit_USBD_MIDI& midi) noexcept : _midi{midi} {}

    auto send(const MidiDeliveryAttempt& attempt) -> SendResult override {
        if (!TinyUSBDevice.mounted()) {
            return SendResult::Disconnected;
        }
        const auto packet = encodeMidiUsbPacket(attempt.message);
        return _midi.writePacket(packet.data()) ? SendResult::Accepted : SendResult::RetryLater;
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
