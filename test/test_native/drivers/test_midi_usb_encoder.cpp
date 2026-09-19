#include "test_midi_usb_encoder.h"

#include "drivers/midi_usb_encoder.h"

#include <unity.h>

namespace {

void test_encoder_preserves_supported_message_bytes() {
    constexpr auto noteOn = *SwingMetro::MidiMessage::noteOn(15, 127, 127);
    constexpr auto noteOff = *SwingMetro::MidiMessage::noteOff(0, 0);
    constexpr auto zeroVelocityNoteOn = *SwingMetro::MidiMessage::noteOn(15, 127, 0);
    constexpr auto encodedNoteOn = SwingMetro::encodeMidiUsbPacket(noteOn);
    constexpr auto encodedNoteOff = SwingMetro::encodeMidiUsbPacket(noteOff);
    constexpr auto encodedZeroVelocity = SwingMetro::encodeMidiUsbPacket(zeroVelocityNoteOn);
    TEST_ASSERT_EQUAL_HEX8(0x09, encodedNoteOn[0]);
    TEST_ASSERT_EQUAL_HEX8(0x9F, encodedNoteOn[1]);
    TEST_ASSERT_EQUAL_UINT8(127, encodedNoteOn[2]);
    TEST_ASSERT_EQUAL_UINT8(127, encodedNoteOn[3]);
    TEST_ASSERT_EQUAL_HEX8(0x08, encodedNoteOff[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80, encodedNoteOff[1]);
    TEST_ASSERT_EQUAL_UINT8(0, encodedNoteOff[2]);
    TEST_ASSERT_EQUAL_UINT8(0, encodedNoteOff[3]);
    TEST_ASSERT_EQUAL_HEX8(0x09, encodedZeroVelocity[0]);
    TEST_ASSERT_EQUAL_HEX8(0x9F, encodedZeroVelocity[1]);
    TEST_ASSERT_EQUAL_UINT8(127, encodedZeroVelocity[2]);
    TEST_ASSERT_EQUAL_UINT8(0, encodedZeroVelocity[3]);
    TEST_ASSERT_TRUE(zeroVelocityNoteOn.isNoteOffEquivalent());
}

void test_encoder_preserves_realtime_bytes_and_validation_contract() {
    constexpr auto start = SwingMetro::encodeMidiUsbPacket(SwingMetro::MidiMessage::start());
    constexpr auto continuePacket =
        SwingMetro::encodeMidiUsbPacket(SwingMetro::MidiMessage::continuePlayback());
    constexpr auto stop = SwingMetro::encodeMidiUsbPacket(SwingMetro::MidiMessage::stop());
    constexpr auto clock = SwingMetro::encodeMidiUsbPacket(SwingMetro::MidiMessage::clock());
    TEST_ASSERT_EQUAL_HEX8(0xFA, start[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFB, continuePacket[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, stop[1]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, clock[1]);
    TEST_ASSERT_FALSE(SwingMetro::MidiMessage::noteOn(16, 0, 0).has_value());
    TEST_ASSERT_FALSE(SwingMetro::MidiMessage::noteOn(0, 128, 0).has_value());
    TEST_ASSERT_FALSE(SwingMetro::MidiMessage::noteOn(0, 0, 128).has_value());
    TEST_ASSERT_FALSE(SwingMetro::MidiMessage::noteOff(16, 0).has_value());
}

} // namespace

void test_midi_usb_encoder_main() {
    RUN_TEST(test_encoder_preserves_supported_message_bytes);
    RUN_TEST(test_encoder_preserves_realtime_bytes_and_validation_contract);
}
