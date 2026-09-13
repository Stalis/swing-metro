#include "test_external_midi_clock.h"

#include "engine/external_midi_clock.h"
#include "engine/midi_event_queue.h"
#include "engine/midi_step_boundary.h"
#include "engine/sequencer.h"
#include "engine/transport_tick_consumer.h"

#include <array>
#include <cstdint>
#include <unity.h>

namespace {

using SwingMetro::ExternalMidiClock;
using SwingMetro::MidiEventQueue;
using SwingMetro::MidiRealtimeEvent;
using SwingMetro::MidiRealtimeEventType;
using SwingMetro::MidiUsbPacket;
using SwingMetro::phaseFromPercent;
using SwingMetro::Transport;
using SwingMetro::TransportTickConsumer;
using SwingMetro::TransportTickRecord;
using SwingMetro::TransportTickSource;

constexpr auto event(MidiRealtimeEventType type, std::uint32_t timestampUs) -> MidiRealtimeEvent {
    return {type, timestampUs};
}

void test_packet_parser_accepts_only_single_byte_realtime_messages() {
    constexpr std::uint8_t clock[] = {0x0F, 0xF8, 0, 0};
    constexpr std::uint8_t start[] = {0x0F, 0xFA, 0, 0};
    constexpr std::uint8_t songPosition[] = {0x03, 0xF2, 0, 0};

    const auto parsedClock = SwingMetro::midiRealtimeEventFromUsbPacket(clock, 123);
    const auto parsedStart = SwingMetro::midiRealtimeEventFromUsbPacket(start, 456);
    TEST_ASSERT_TRUE(parsedClock.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MidiRealtimeEventType::Clock),
                            static_cast<uint8_t>(parsedClock->type));
    TEST_ASSERT_EQUAL_UINT32(123, parsedClock->timestampUs);
    TEST_ASSERT_TRUE(parsedStart.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MidiRealtimeEventType::Start),
                            static_cast<uint8_t>(parsedStart->type));
    TEST_ASSERT_FALSE(SwingMetro::midiRealtimeEventFromUsbPacket(songPosition, 0).has_value());
}

void test_waiting_lock_filter_loss_relock_and_wrap() {
    ExternalMidiClock clock;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Waiting),
                            static_cast<uint8_t>(clock.status()));

    constexpr std::uint32_t first = 0xFFFFF000;
    clock.handle(event(MidiRealtimeEventType::Clock, first));
    clock.handle(event(MidiRealtimeEventType::Clock, first + 20'833));
    clock.handle(event(MidiRealtimeEventType::Clock, first + 41'900));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));
    TEST_ASSERT_INT_WITHIN(1, 120, clock.bpm());

    clock.update(first + 41'900 + ExternalMidiClock::kClockLossTimeoutUs - 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));
    clock.update(first + 41'900 + ExternalMidiClock::kClockLossTimeoutUs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Lost),
                            static_cast<uint8_t>(clock.status()));
    TEST_ASSERT_EQUAL_UINT8(0, clock.bpm());
    clock.handle(event(MidiRealtimeEventType::Clock, first + 62'733));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));
    TEST_ASSERT_EQUAL_UINT8(0, clock.bpm());
}

void test_older_loop_timestamp_does_not_immediately_lose_new_clock() {
    ExternalMidiClock clock;
    constexpr std::uint32_t loopStartUs = 1'000'000;
    constexpr std::uint32_t acceptedClockUs = loopStartUs + 100;

    clock.handle(event(MidiRealtimeEventType::Clock, acceptedClockUs));
    clock.update(loopStartUs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));

    clock.update(acceptedClockUs + ExternalMidiClock::kClockLossTimeoutUs - 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));
    clock.update(acceptedClockUs + ExternalMidiClock::kClockLossTimeoutUs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Lost),
                            static_cast<uint8_t>(clock.status()));
}

void test_period_smoothing_ignores_outliers_and_reports_fractional_bpm() {
    ExternalMidiClock clock;
    clock.handle(event(MidiRealtimeEventType::Clock, 0));
    clock.handle(event(MidiRealtimeEventType::Clock, 20'800));
    TEST_ASSERT_EQUAL_UINT32(20'800, clock.periodUs());
    TEST_ASSERT_EQUAL_UINT32(120'192, clock.bpmMilli());

    clock.handle(event(MidiRealtimeEventType::Clock, 41'700));
    TEST_ASSERT_INT_WITHIN(30, 20'825, clock.periodUs());
    const auto periodBeforeOutlier = clock.periodUs();
    clock.handle(event(MidiRealtimeEventType::Clock, 42'700));
    TEST_ASSERT_EQUAL_UINT32(periodBeforeOutlier, clock.periodUs());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));
}

void test_start_preserves_estimate_and_continue_runs_after_one_relock_tick() {
    ExternalMidiClock clock;
    clock.handle(event(MidiRealtimeEventType::Clock, 0));
    clock.handle(event(MidiRealtimeEventType::Clock, 20'833));
    const auto savedPeriod = clock.periodUs();
    clock.handle(event(MidiRealtimeEventType::Start, 21'000));
    TEST_ASSERT_EQUAL_UINT32(savedPeriod, clock.periodUs());

    clock.update(20'833 + ExternalMidiClock::kClockLossTimeoutUs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Lost),
                            static_cast<uint8_t>(clock.status()));
    const auto relock = clock.handle(event(MidiRealtimeEventType::Clock, 300'000));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));
    TEST_ASSERT_FALSE(relock.tick);
    TEST_ASSERT_TRUE(clock.handle(event(MidiRealtimeEventType::Continue, 300'001)).started);
    TEST_ASSERT_TRUE(clock.handle(event(MidiRealtimeEventType::Clock, 320'833)).tick);
}

void test_start_first_clock_waits_for_period_before_publishing_tick() {
    ExternalMidiClock clock;
    clock.handle(event(MidiRealtimeEventType::Start, 0));

    const auto firstClock = clock.handle(event(MidiRealtimeEventType::Clock, 1'000));
    TEST_ASSERT_FALSE(firstClock.tick);
    TEST_ASSERT_EQUAL_UINT32(0, firstClock.tickRecord.periodUs);

    const auto secondClock = clock.handle(event(MidiRealtimeEventType::Clock, 21'833));
    TEST_ASSERT_TRUE(secondClock.tick);
    TEST_ASSERT_EQUAL_UINT32(20'833, secondClock.tickRecord.periodUs);
}

void test_external_consumer_never_echoes_realtime_or_crosses_early_tick_boundary() {
    Transport transport;
    MidiEventQueue queue;
    TransportTickConsumer consumer{transport, queue, TransportTickSource::External};
    std::array<MidiUsbPacket, 8> packets{};
    std::size_t count = 0;
    const auto send = [&](const MidiUsbPacket& packet) { packets[count++] = packet; };

    consumer.start(send);
    TEST_ASSERT_EQUAL_UINT32(0, count);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phaseFromPercent(50)}, {0x09, 0x90, 0x3C, 0x40}));
    consumer.consumeTick({1'000, 20'000}, 1'000, send);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({1, phaseFromPercent(50)}, {0x09, 0x90, 0x3C, 0x40}));
    consumer.consumeTick({9'000, 20'000}, 9'000, send);
    TEST_ASSERT_EQUAL_UINT32(0, count);

    consumer.dispatchDue(19'000, send);
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL_HEX8(0x90, packets[0][1]);
    consumer.stop(send);
    TEST_ASSERT_EQUAL_UINT32(1, count);
}

void test_external_consumer_sends_phase_before_a_late_tick_boundary() {
    Transport transport;
    MidiEventQueue queue;
    TransportTickConsumer consumer{transport, queue, TransportTickSource::External};
    std::array<MidiUsbPacket, 4> packets{};
    std::size_t count = 0;
    const auto send = [&](const MidiUsbPacket& packet) { packets[count++] = packet; };

    consumer.start(send);
    TEST_ASSERT_EQUAL(SwingMetro::MidiEventQueueEnqueueResult::Ok,
                      queue.enqueue({0, phaseFromPercent(50)}, {0x09, 0x90, 0x3C, 0x40}));
    consumer.consumeTick({1'000, 20'000}, 1'000, send);
    consumer.dispatchDue(11'000, send);
    consumer.consumeTick({12'000, 20'000}, 12'000, send);
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL_HEX8(0x90, packets[0][1]);
}

void test_start_immediately_plays_first_step_then_advances_after_six_ticks() {
    ExternalMidiClock clock;
    Sequencer sequencer;
    sequencer.toggleStep(0);
    bool noteSent = false;
    uint8_t lastNote = 0;
    std::array<uint8_t, 4> notes{};
    uint8_t noteCount = 0;
    const auto process = [&](const SwingMetro::ExternalMidiClockResult& result) {
        SwingMetro::processExternalMidiClock(
            sequencer, result, noteSent, lastNote,
            [&](uint8_t note) { notes[noteCount++] = static_cast<uint8_t>(0x80 | note); },
            [&](uint8_t note, uint8_t) { notes[noteCount++] = static_cast<uint8_t>(0x90 | note); });
    };

    process(clock.handle(event(MidiRealtimeEventType::Start, 0)));
    TEST_ASSERT_TRUE(sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT8(0, *sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(1, noteCount);
    for (uint8_t tick = 1; tick < 6; ++tick) {
        process(clock.handle(event(MidiRealtimeEventType::Clock, tick * 20'833U)));
    }
    TEST_ASSERT_EQUAL_UINT8(0, *sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(1, noteCount);
    process(clock.handle(event(MidiRealtimeEventType::Clock, 6 * 20'833U)));
    TEST_ASSERT_EQUAL_UINT8(1, *sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(2, noteCount);

    process(clock.handle(event(MidiRealtimeEventType::Stop, 130'000)));
    TEST_ASSERT_FALSE(sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT8(2, noteCount);
    process(clock.handle(event(MidiRealtimeEventType::Continue, 140'000)));
    TEST_ASSERT_TRUE(sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT8(1, *sequencer.getDisplayStepIndex());
}

void test_external_mode_never_uses_internal_scheduler() {
    Sequencer sequencer;
    sequencer.sync(0);
    SwingMetro::MidiClockTransmitter transmitter;
    class Sink final : public SwingMetro::MidiRealTimeSink {
      public:
        auto send(uint8_t) -> void override {}
    } sink;
    bool noteSent = false;
    uint8_t lastNote = 0;
    transmitter.transition(1'000'000, sequencer.getBpm(), SwingMetro::MidiClockMode::External,
                           sequencer.isRunning(), sink);
    TEST_ASSERT_FALSE(sequencer.getDisplayStepIndex().has_value());
}

void test_legacy_step_compatibility_still_advances_after_six_ticks() {
    ExternalMidiClock clock;
    Sequencer sequencer;
    bool noteSent = false;
    uint8_t lastNote = 0;
    const auto process = [&](const SwingMetro::ExternalMidiClockResult& result) {
        SwingMetro::processExternalMidiClock(
            sequencer, result, noteSent, lastNote, [](uint8_t) {}, [](uint8_t, uint8_t) {});
    };

    process(clock.handle(event(MidiRealtimeEventType::Start, 0)));
    for (uint8_t tick = 1; tick <= 5; ++tick) {
        process(clock.handle(event(MidiRealtimeEventType::Clock, tick * 20'833U)));
    }
    process(clock.update(5 * 20'833U + ExternalMidiClock::kClockLossTimeoutUs));
    TEST_ASSERT_FALSE(sequencer.isRunning());

    process(clock.handle(event(MidiRealtimeEventType::Clock, 500'000)));
    process(clock.handle(event(MidiRealtimeEventType::Continue, 500'001)));
    for (uint8_t tick = 1; tick < 6; ++tick) {
        process(clock.handle(event(MidiRealtimeEventType::Clock, 500'000U + tick * 20'833U)));
    }
    TEST_ASSERT_EQUAL_UINT8(0, *sequencer.getDisplayStepIndex());
    process(clock.handle(event(MidiRealtimeEventType::Clock, 500'000U + 6 * 20'833U)));
    TEST_ASSERT_EQUAL_UINT8(1, *sequencer.getDisplayStepIndex());
}

} // namespace

void test_external_midi_clock_main() {
    RUN_TEST(test_packet_parser_accepts_only_single_byte_realtime_messages);
    RUN_TEST(test_waiting_lock_filter_loss_relock_and_wrap);
    RUN_TEST(test_older_loop_timestamp_does_not_immediately_lose_new_clock);
    RUN_TEST(test_period_smoothing_ignores_outliers_and_reports_fractional_bpm);
    RUN_TEST(test_start_preserves_estimate_and_continue_runs_after_one_relock_tick);
    RUN_TEST(test_start_first_clock_waits_for_period_before_publishing_tick);
    RUN_TEST(test_external_consumer_never_echoes_realtime_or_crosses_early_tick_boundary);
    RUN_TEST(test_external_consumer_sends_phase_before_a_late_tick_boundary);
    RUN_TEST(test_start_immediately_plays_first_step_then_advances_after_six_ticks);
    RUN_TEST(test_external_mode_never_uses_internal_scheduler);
    RUN_TEST(test_legacy_step_compatibility_still_advances_after_six_ticks);
}
