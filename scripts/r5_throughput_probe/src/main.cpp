#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

#include "drivers/usb_midi_adapter.h"

#include <cstdint>

namespace {
constexpr std::uint32_t STEP_US = 62'500;
constexpr std::uint32_t REPEATS_PER_STEP = 16;
constexpr std::uint32_t BURST_COUNT = 257;
constexpr std::uint32_t VOICES = 16 * 16;

Adafruit_USBD_MIDI usbMidi;
SwingMetro::UsbMidiMessageSink sink{usbMidi};

bool started = false;
bool finished = false;
std::uint32_t startUs = 0;
std::uint32_t burst = 0;
std::uint32_t messageIndex = 0;
std::uint32_t accepted = 0;
std::uint32_t retryLater = 0;
std::uint32_t disconnected = 0;
std::uint32_t lateOver1ms = 0;
std::uint32_t maxFirstAttemptLateUs = 0;
std::uint32_t maxAcceptLateUs = 0;
std::uint32_t maxPassUs = 0;
std::uint32_t firstAttemptUs = 0;
bool attempted = false;

auto dueUs() -> std::uint32_t {
    return startUs + static_cast<std::uint32_t>((static_cast<std::uint64_t>(burst) * STEP_US) /
                                                REPEATS_PER_STEP);
}

auto burstSize() -> std::uint32_t {
    return burst == 0 || burst == BURST_COUNT - 1 ? VOICES : 2 * VOICES;
}

auto makeMessage() -> SwingMetro::MidiMessage {
    const bool off = burst == BURST_COUNT - 1 || (burst != 0 && messageIndex < VOICES);
    const auto voice = burst == 0 ? messageIndex : messageIndex % VOICES;
    const auto channel = static_cast<std::uint8_t>(voice % 16);
    const auto note = static_cast<std::uint8_t>(36 + voice / 16);
    return off ? *SwingMetro::MidiMessage::noteOff(channel, note)
               : *SwingMetro::MidiMessage::noteOn(channel, note, 100);
}

auto finish() -> void {
    if (finished) {
        return;
    }
    finished = true;
    Serial.print("R5_SPIKE,accepted=");
    Serial.print(accepted);
    Serial.print(",expected=");
    Serial.print(2 * VOICES + (BURST_COUNT - 2) * 2 * VOICES);
    Serial.print(",retry_later=");
    Serial.print(retryLater);
    Serial.print(",disconnected=");
    Serial.print(disconnected);
    Serial.print(",late_over_1ms=");
    Serial.print(lateOver1ms);
    Serial.print(",max_first_attempt_late_us=");
    Serial.print(maxFirstAttemptLateUs);
    Serial.print(",max_accept_late_us=");
    Serial.print(maxAcceptLateUs);
    Serial.print(",max_pass_us=");
    Serial.println(maxPassUs);
}
} // namespace

void setup() {
    Serial.begin(115200);
    if (!TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.begin(0);
    }
    usbMidi.setStringDescriptor("Swing Metro R5 Spike");
    usbMidi.begin();
    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(10);
        TinyUSBDevice.attach();
    }
}

void loop() {
    if (finished) {
        return;
    }
    if (!started) {
        if (Serial.available() && Serial.read() == 'g') {
            started = true;
            startUs = micros() + 100'000;
            Serial.println("R5_SPIKE_START");
        }
        return;
    }

    const auto passStartedUs = micros();
    if (burst < BURST_COUNT && static_cast<std::int32_t>(passStartedUs - dueUs()) >= 0) {
        while (messageIndex < burstSize()) {
            const auto attemptAtUs = micros();
            if (!attempted) {
                firstAttemptUs = attemptAtUs;
                attempted = true;
                const auto firstLate = attemptAtUs - dueUs();
                if (firstLate > maxFirstAttemptLateUs) {
                    maxFirstAttemptLateUs = firstLate;
                }
            }
            SwingMetro::MidiDeliveryAttempt attempt{};
            attempt.message = makeMessage();
            const auto result = sink.send(attempt);
            if (result == SwingMetro::SendResult::RetryLater) {
                ++retryLater;
                break;
            }
            if (result == SwingMetro::SendResult::Disconnected) {
                ++disconnected;
                finish();
                return;
            }
            const auto acceptLate = micros() - dueUs();
            if (acceptLate > maxAcceptLateUs) {
                maxAcceptLateUs = acceptLate;
            }
            if (acceptLate > 1'000) {
                ++lateOver1ms;
            }
            ++accepted;
            ++messageIndex;
            attempted = false;
        }
        if (messageIndex == burstSize()) {
            messageIndex = 0;
            attempted = false;
            ++burst;
        }
    }

    const auto passUs = micros() - passStartedUs;
    if (passUs > maxPassUs) {
        maxPassUs = passUs;
    }
    if (burst == BURST_COUNT) {
        finish();
    }
}
