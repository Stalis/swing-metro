#include "test_transport.h"

#include "engine/transport.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <unity.h>

namespace {

using SwingMetro::Transport;
using SwingMetro::TransportPosition;
using SwingMetro::TransportTick;

void test_phase_from_percent_maps_fraction_of_tick() {
    TEST_ASSERT_EQUAL_UINT16(0, SwingMetro::phaseFromPercent(0));
    TEST_ASSERT_EQUAL_UINT16(32768, SwingMetro::phaseFromPercent(50));
    TEST_ASSERT_EQUAL_UINT16(49152, SwingMetro::phaseFromPercent(75));
    TEST_ASSERT_EQUAL_UINT16(SwingMetro::PHASE_MAX, SwingMetro::phaseFromPercent(100));
    TEST_ASSERT_EQUAL_UINT16(SwingMetro::PHASE_MAX, SwingMetro::phaseFromPercent(255));
}

void test_positions_compare_lexicographically_and_keep_equal_order() {
    constexpr TransportPosition before{12, SwingMetro::PHASE_MAX};
    constexpr TransportPosition after{13, 0};
    TEST_ASSERT_TRUE(before < after);

    struct Event {
        TransportPosition position;
        std::uint8_t order;
    };
    std::array<Event, 3> events{{{{2, 0}, 1}, {{1, 0}, 0}, {{2, 0}, 2}}};
    std::stable_sort(events.begin(), events.end(), [](const Event& left, const Event& right) {
        return left.position < right.position;
    });

    TEST_ASSERT_EQUAL_UINT8(0, events[0].order);
    TEST_ASSERT_EQUAL_UINT8(1, events[1].order);
    TEST_ASSERT_EQUAL_UINT8(2, events[2].order);
}

void test_tick_constants_match_midi_resolution() {
    TEST_ASSERT_EQUAL_UINT64(24, SwingMetro::TICKS_PER_QUARTER);
    TEST_ASSERT_EQUAL_UINT64(6, SwingMetro::TICKS_PER_SIXTEENTH);
}

void test_start_stop_continue_and_reset_position() {
    Transport transport;
    TEST_ASSERT_FALSE(transport.snapshot().running);
    TEST_ASSERT_FALSE(transport.advanceTick());

    transport.start();
    TEST_ASSERT_TRUE(transport.snapshot().running);
    TEST_ASSERT_EQUAL_UINT64(0, transport.position().tick);
    TEST_ASSERT_EQUAL_UINT16(0, transport.position().phase);
    TEST_ASSERT_TRUE(transport.advanceTick());

    transport.stop();
    TEST_ASSERT_FALSE(transport.snapshot().running);
    TEST_ASSERT_FALSE(transport.advanceTick());
    transport.continuePlayback();
    TEST_ASSERT_TRUE(transport.snapshot().running);
    TEST_ASSERT_EQUAL_UINT64(1, transport.position().tick);

    transport.resetPosition();
    TEST_ASSERT_TRUE(transport.snapshot().running);
    TEST_ASSERT_EQUAL_UINT64(0, transport.position().tick);
}

void test_snapshot_tracks_ticks_within_sixteenth() {
    Transport transport;
    transport.start();
    for (TransportTick tick = 0; tick < SwingMetro::TICKS_PER_SIXTEENTH; ++tick) {
        TEST_ASSERT_EQUAL_UINT8(tick, transport.snapshot().tickInSixteenth);
        TEST_ASSERT_TRUE(transport.advanceTick());
    }
    TEST_ASSERT_EQUAL_UINT8(0, transport.snapshot().tickInSixteenth);
}

void test_advance_tick_saturates_at_maximum_tick() {
    Transport transport(std::numeric_limits<TransportTick>::max());
    transport.continuePlayback();

    TEST_ASSERT_FALSE(transport.advanceTick());
    TEST_ASSERT_EQUAL_UINT64(std::numeric_limits<TransportTick>::max(), transport.position().tick);
}

void test_gate_duration_uses_full_phase_count_and_assigned_position() {
    constexpr TransportPosition on{12, SwingMetro::phaseFromPercent(75)};
    TEST_ASSERT_EQUAL_UINT64(3'932, SwingMetro::gateDurationUnits(1));
    TEST_ASSERT_EQUAL_UINT64(98'304, SwingMetro::gateDurationUnits(25));
    TEST_ASSERT_EQUAL_UINT64(196'608, SwingMetro::gateDurationUnits(50));
    TEST_ASSERT_EQUAL_UINT64(294'912, SwingMetro::gateDurationUnits(75));
    TEST_ASSERT_EQUAL_UINT64(393'216, SwingMetro::gateDurationUnits(100));

    const auto deadline = SwingMetro::gateDeadline(on, 100);
    TEST_ASSERT_EQUAL_UINT64(18, deadline.tick);
    TEST_ASSERT_EQUAL_UINT16(on.phase, deadline.phase);
}

void test_phase_unit_addition_carries_and_saturates_without_overflow() {
    const auto carried = SwingMetro::addPhaseUnits({7, SwingMetro::PHASE_MAX}, 1);
    TEST_ASSERT_EQUAL_UINT64(8, carried.tick);
    TEST_ASSERT_EQUAL_UINT16(0, carried.phase);

    const auto saturated = SwingMetro::gateDeadline(
        {std::numeric_limits<TransportTick>::max() - 1, SwingMetro::PHASE_MAX}, 100);
    TEST_ASSERT_EQUAL_UINT64(std::numeric_limits<TransportTick>::max(), saturated.tick);
    TEST_ASSERT_EQUAL_UINT16(SwingMetro::PHASE_MAX, saturated.phase);
}

} // namespace

void test_transport_main() {
    RUN_TEST(test_phase_from_percent_maps_fraction_of_tick);
    RUN_TEST(test_positions_compare_lexicographically_and_keep_equal_order);
    RUN_TEST(test_tick_constants_match_midi_resolution);
    RUN_TEST(test_start_stop_continue_and_reset_position);
    RUN_TEST(test_snapshot_tracks_ticks_within_sixteenth);
    RUN_TEST(test_advance_tick_saturates_at_maximum_tick);
    RUN_TEST(test_gate_duration_uses_full_phase_count_and_assigned_position);
    RUN_TEST(test_phase_unit_addition_carries_and_saturates_without_overflow);
}
