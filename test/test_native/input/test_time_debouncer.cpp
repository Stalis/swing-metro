#include "test_time_debouncer.h"

#include <time_debouncer.h>

#include <cstdint>
#include <limits>
#include <unity.h>

namespace {

void test_stable_press_and_release_confirm_once() {
    InputTiming::TimeDebouncer debouncer;

    TEST_ASSERT_FALSE(debouncer.observe(true, 100, 15).has_value());
    TEST_ASSERT_FALSE(debouncer.observe(true, 114, 15).has_value());
    const auto pressed = debouncer.observe(true, 115, 15);
    TEST_ASSERT_TRUE(pressed.has_value());
    TEST_ASSERT_TRUE(*pressed);
    TEST_ASSERT_FALSE(debouncer.observe(true, 130, 15).has_value());

    TEST_ASSERT_FALSE(debouncer.observe(false, 200, 15).has_value());
    const auto released = debouncer.observe(false, 215, 15);
    TEST_ASSERT_TRUE(released.has_value());
    TEST_ASSERT_FALSE(*released);
    TEST_ASSERT_FALSE(debouncer.observe(false, 230, 15).has_value());
}

void test_bounce_restarts_candidate_interval() {
    InputTiming::TimeDebouncer debouncer;

    TEST_ASSERT_FALSE(debouncer.observe(true, 100, 15).has_value());
    TEST_ASSERT_FALSE(debouncer.observe(false, 105, 15).has_value());
    TEST_ASSERT_FALSE(debouncer.observe(true, 110, 15).has_value());
    TEST_ASSERT_FALSE(debouncer.observe(true, 124, 15).has_value());
    const auto pressed = debouncer.observe(true, 125, 15);

    TEST_ASSERT_TRUE(pressed.has_value());
    TEST_ASSERT_TRUE(*pressed);
}

void test_short_pulse_and_candidate_cancellation_emit_nothing() {
    InputTiming::TimeDebouncer debouncer;

    TEST_ASSERT_FALSE(debouncer.observe(true, 100, 15).has_value());
    TEST_ASSERT_FALSE(debouncer.observe(false, 110, 15).has_value());
    TEST_ASSERT_FALSE(debouncer.observe(false, 200, 15).has_value());
}

void test_delayed_observation_confirms_at_its_own_timestamp() {
    InputTiming::TimeDebouncer debouncer;

    TEST_ASSERT_FALSE(debouncer.observe(true, 100, 15).has_value());
    const auto pressed = debouncer.observe(true, 137, 15);

    TEST_ASSERT_TRUE(pressed.has_value());
    TEST_ASSERT_TRUE(*pressed);
}

void test_wraparound_preserves_elapsed_time() {
    InputTiming::TimeDebouncer debouncer;
    constexpr auto candidateSince = std::numeric_limits<std::uint32_t>::max() - 9U;

    TEST_ASSERT_FALSE(debouncer.observe(true, candidateSince, 15).has_value());
    const auto pressed = debouncer.observe(true, 5, 15);

    TEST_ASSERT_TRUE(pressed.has_value());
    TEST_ASSERT_TRUE(*pressed);
}

void test_zero_duration_confirms_on_first_observation() {
    InputTiming::TimeDebouncer debouncer;

    const auto pressed = debouncer.observe(true, 100, 0);
    TEST_ASSERT_TRUE(pressed.has_value());
    TEST_ASSERT_TRUE(*pressed);
}

void test_reset_seeds_state_without_an_edge() {
    InputTiming::TimeDebouncer debouncer;
    debouncer.reset(true);

    TEST_ASSERT_FALSE(debouncer.observe(true, 100, 15).has_value());
    TEST_ASSERT_FALSE(debouncer.observe(false, 110, 15).has_value());
    const auto released = debouncer.observe(false, 125, 15);
    TEST_ASSERT_TRUE(released.has_value());
    TEST_ASSERT_FALSE(*released);
}

} // namespace

void test_time_debouncer_main() {
    RUN_TEST(test_stable_press_and_release_confirm_once);
    RUN_TEST(test_bounce_restarts_candidate_interval);
    RUN_TEST(test_short_pulse_and_candidate_cancellation_emit_nothing);
    RUN_TEST(test_delayed_observation_confirms_at_its_own_timestamp);
    RUN_TEST(test_wraparound_preserves_elapsed_time);
    RUN_TEST(test_zero_duration_confirms_on_first_observation);
    RUN_TEST(test_reset_seeds_state_without_an_edge);
}
