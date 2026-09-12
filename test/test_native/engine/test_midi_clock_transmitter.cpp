#include "test_midi_clock_transmitter.h"

#include "engine/midi_clock_transmitter.h"
#include "engine/midi_step_boundary.h"
#include "engine/midi_usb_packet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unity.h>

namespace {

class Sink final : public SwingMetro::MidiRealTimeSink {
  public:
    auto send(std::uint8_t status) -> void override { statuses[size++] = status; }

    std::array<std::uint8_t, 32> statuses{};
    std::size_t size = 0;
};

class EventSink final : public SwingMetro::MidiRealTimeSink {
  public:
    auto send(std::uint8_t status) -> void override { events[size++] = status; }

    auto sendNoteOff(std::uint8_t note) -> void {
        events[size++] = static_cast<std::uint8_t>(0x80 | note);
    }
    auto sendNoteOn(std::uint8_t note, std::uint8_t) -> void {
        events[size++] = static_cast<std::uint8_t>(0x90 | note);
    }

    std::array<std::uint8_t, 32> events{};
    std::size_t size = 0;
};

void test_usb_real_time_packets_use_single_byte_cin() {
    constexpr auto clock = SwingMetro::usbMidiRealTimePacket(0xF8);
    constexpr auto start = SwingMetro::usbMidiRealTimePacket(0xFA);
    constexpr auto stop = SwingMetro::usbMidiRealTimePacket(0xFC);

    TEST_ASSERT_EQUAL_HEX8(0x0F, clock[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, clock[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, clock[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, clock[3]);
    TEST_ASSERT_EQUAL_HEX8(0x0F, start[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFA, start[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0F, stop[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, stop[1]);
}

void test_24_ppqn_periods_at_tempo_limits() {
    SwingMetro::MidiClockTransmitter clock;
    Sink sink;

    clock.tick(0, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    clock.tick(20'832, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    TEST_ASSERT_EQUAL_UINT32(1, sink.size);
    clock.tick(20'833, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    clock.tick(41'666, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    clock.tick(62'500, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    TEST_ASSERT_EQUAL_UINT32(4, sink.size);

    SwingMetro::MidiClockTransmitter slow;
    Sink slowSink;
    slow.tick(0, 40, SwingMetro::MidiClockMode::Internal, true, slowSink);
    slow.tick(62'499, 40, SwingMetro::MidiClockMode::Internal, true, slowSink);
    slow.tick(62'500, 40, SwingMetro::MidiClockMode::Internal, true, slowSink);
    TEST_ASSERT_EQUAL_UINT32(2, slowSink.size);

    SwingMetro::MidiClockTransmitter fast;
    Sink fastSink;
    fast.tick(0, 240, SwingMetro::MidiClockMode::Internal, true, fastSink);
    fast.tick(10'416, 240, SwingMetro::MidiClockMode::Internal, true, fastSink);
    fast.tick(20'833, 240, SwingMetro::MidiClockMode::Internal, true, fastSink);
    TEST_ASSERT_EQUAL_UINT32(3, fastSink.size);
}

void test_transitions_reset_phase_without_duplicates() {
    SwingMetro::MidiClockTransmitter clock;
    Sink sink;

    clock.tick(0, 120, SwingMetro::MidiClockMode::Off, true, sink);
    clock.tick(1, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    clock.tick(2, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    TEST_ASSERT_EQUAL_UINT32(1, sink.size);
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.statuses[0]);

    const auto stopped = clock.tick(3, 120, SwingMetro::MidiClockMode::Internal, false, sink);
    TEST_ASSERT_TRUE(stopped.transportStopped);
    clock.tick(4, 120, SwingMetro::MidiClockMode::Internal, false, sink);
    TEST_ASSERT_EQUAL_UINT32(2, sink.size);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.statuses[1]);

    const auto started = clock.tick(5, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    TEST_ASSERT_TRUE(started.transportStarted);
    clock.tick(20'838, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    TEST_ASSERT_EQUAL_UINT32(4, sink.size);
    TEST_ASSERT_EQUAL_HEX8(0xFA, sink.statuses[2]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, sink.statuses[3]);

    clock.tick(20'839, 120, SwingMetro::MidiClockMode::External, true, sink);
    clock.tick(20'840, 120, SwingMetro::MidiClockMode::Off, true, sink);
    TEST_ASSERT_EQUAL_UINT32(5, sink.size);
    TEST_ASSERT_EQUAL_HEX8(0xFC, sink.statuses[4]);
}

void test_bpm_change_wraparound_and_limited_catch_up() {
    SwingMetro::MidiClockTransmitter clock;
    Sink sink;
    clock.tick(0, 120, SwingMetro::MidiClockMode::Internal, true, sink);
    clock.tick(10'000, 240, SwingMetro::MidiClockMode::Internal, true, sink);
    TEST_ASSERT_EQUAL_UINT32(1, sink.size);
    clock.tick(10'416, 240, SwingMetro::MidiClockMode::Internal, true, sink);
    TEST_ASSERT_EQUAL_UINT32(2, sink.size);

    SwingMetro::MidiClockTransmitter wrapped;
    Sink wrappedSink;
    constexpr std::uint32_t start = 0xFFFFF000;
    wrapped.tick(start, 120, SwingMetro::MidiClockMode::Internal, true, wrappedSink);
    wrapped.tick(start + 20'833, 120, SwingMetro::MidiClockMode::Internal, true, wrappedSink);
    TEST_ASSERT_EQUAL_UINT32(2, wrappedSink.size);

    SwingMetro::MidiClockTransmitter delayed;
    Sink delayedSink;
    delayed.tick(0, 120, SwingMetro::MidiClockMode::Internal, true, delayedSink);
    delayed.tick(208'330, 120, SwingMetro::MidiClockMode::Internal, true, delayedSink);
    TEST_ASSERT_EQUAL_UINT32(5, delayedSink.size);
    delayed.tick(208'330, 120, SwingMetro::MidiClockMode::Internal, true, delayedSink);
    TEST_ASSERT_EQUAL_UINT32(5, delayedSink.size);
}

void test_step_boundary_orders_transport_before_notes() {
    Sequencer sequencer;
    sequencer.toggleStep(0);
    sequencer.sync(0);

    SwingMetro::MidiClockTransmitter clock;
    EventSink sink;
    bool noteSent = false;
    std::uint8_t lastNoteSent = 0;

    SwingMetro::processMidiStepBoundary(
        sequencer, 0, SwingMetro::MidiClockMode::Internal, clock, sink, noteSent, lastNoteSent,
        [&](std::uint8_t note) { sink.sendNoteOff(note); },
        [&](std::uint8_t note, std::uint8_t velocity) { sink.sendNoteOn(note, velocity); });

    TEST_ASSERT_EQUAL_UINT32(2, sink.size);
    TEST_ASSERT_EQUAL_HEX8(SwingMetro::MidiClockTransmitter::kStartStatus, sink.events[0]);
    TEST_ASSERT_EQUAL_HEX8(0x90 | getNote(Note::C, 0), sink.events[1]);

    sequencer.toggleRunning(1);
    SwingMetro::processMidiStepBoundary(
        sequencer, 1, SwingMetro::MidiClockMode::Internal, clock, sink, noteSent, lastNoteSent,
        [&](std::uint8_t note) { sink.sendNoteOff(note); },
        [&](std::uint8_t note, std::uint8_t velocity) { sink.sendNoteOn(note, velocity); });

    TEST_ASSERT_EQUAL_UINT32(4, sink.size);
    TEST_ASSERT_EQUAL_HEX8(SwingMetro::MidiClockTransmitter::kStopStatus, sink.events[2]);
    TEST_ASSERT_EQUAL_HEX8(0x80 | getNote(Note::C, 0), sink.events[3]);
}

void test_step_boundary_orders_note_packets_before_sixth_clock() {
    Sequencer sequencer;
    sequencer.toggleStep(0);
    sequencer.toggleStep(1);
    sequencer.sync(0);

    SwingMetro::MidiClockTransmitter clock;
    EventSink sink;
    bool noteSent = false;
    std::uint8_t lastNoteSent = 0;
    const auto process = [&](std::uint32_t nowUs) {
        SwingMetro::processMidiStepBoundary(
            sequencer, nowUs, SwingMetro::MidiClockMode::Internal, clock, sink, noteSent,
            lastNoteSent, [&](std::uint8_t note) { sink.sendNoteOff(note); },
            [&](std::uint8_t note, std::uint8_t velocity) { sink.sendNoteOn(note, velocity); });
    };

    process(0);
    process(20'833);
    process(41'666);
    process(62'500);
    process(83'333);
    process(104'166);
    process(125'000);

    TEST_ASSERT_EQUAL_UINT32(10, sink.size);
    TEST_ASSERT_EQUAL_HEX8(SwingMetro::MidiClockTransmitter::kStartStatus, sink.events[0]);
    TEST_ASSERT_EQUAL_HEX8(0x90 | getNote(Note::C, 0), sink.events[1]);
    TEST_ASSERT_EQUAL_HEX8(0x80 | getNote(Note::C, 0), sink.events[7]);
    TEST_ASSERT_EQUAL_HEX8(0x90 | getNote(Note::C, 0), sink.events[8]);
    TEST_ASSERT_EQUAL_HEX8(SwingMetro::MidiClockTransmitter::kClockStatus, sink.events[9]);
}

} // namespace

void test_midi_clock_transmitter_main() {
    RUN_TEST(test_usb_real_time_packets_use_single_byte_cin);
    RUN_TEST(test_24_ppqn_periods_at_tempo_limits);
    RUN_TEST(test_transitions_reset_phase_without_duplicates);
    RUN_TEST(test_bpm_change_wraparound_and_limited_catch_up);
    RUN_TEST(test_step_boundary_orders_transport_before_notes);
    RUN_TEST(test_step_boundary_orders_note_packets_before_sixth_clock);
}
