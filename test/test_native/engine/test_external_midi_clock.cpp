#include "test_external_midi_clock.h"

#include "engine/external_midi_clock.h"
#include <cstdint>
#include <unity.h>

namespace {

using SwingMetro::ExternalMidiClock;
using SwingMetro::MidiRealtimeEvent;
using SwingMetro::MidiRealtimeEventType;

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

    clock.update(first + 41'900 + ExternalMidiClock::CLOCK_LOSS_TIMEOUT_US - 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));
    clock.update(first + 41'900 + ExternalMidiClock::CLOCK_LOSS_TIMEOUT_US);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Lost),
                            static_cast<uint8_t>(clock.status()));
    TEST_ASSERT_INT_WITHIN(1, 119, clock.bpm());
    clock.handle(event(MidiRealtimeEventType::Clock, first + 62'733));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));
    TEST_ASSERT_INT_WITHIN(1, 119, clock.bpm());
}

void test_older_loop_timestamp_does_not_immediately_lose_new_clock() {
    ExternalMidiClock clock;
    constexpr std::uint32_t loopStartUs = 1'000'000;
    constexpr std::uint32_t acceptedClockUs = loopStartUs + 100;

    clock.handle(event(MidiRealtimeEventType::Clock, acceptedClockUs));
    clock.update(loopStartUs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));

    clock.update(acceptedClockUs + ExternalMidiClock::CLOCK_LOSS_TIMEOUT_US - 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SwingMetro::ExternalMidiClockStatus::Locked),
                            static_cast<uint8_t>(clock.status()));
    clock.update(acceptedClockUs + ExternalMidiClock::CLOCK_LOSS_TIMEOUT_US);
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

    clock.update(20'833 + ExternalMidiClock::CLOCK_LOSS_TIMEOUT_US);
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

} // namespace

void test_external_midi_clock_main() {
    RUN_TEST(test_packet_parser_accepts_only_single_byte_realtime_messages);
    RUN_TEST(test_waiting_lock_filter_loss_relock_and_wrap);
    RUN_TEST(test_older_loop_timestamp_does_not_immediately_lose_new_clock);
    RUN_TEST(test_period_smoothing_ignores_outliers_and_reports_fractional_bpm);
    RUN_TEST(test_start_preserves_estimate_and_continue_runs_after_one_relock_tick);
    RUN_TEST(test_start_first_clock_waits_for_period_before_publishing_tick);
}
