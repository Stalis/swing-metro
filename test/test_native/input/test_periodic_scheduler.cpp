#include "test_periodic_scheduler.h"

#include "input/periodic_scheduler.h"
#include <cstdint>
#include <unity.h>

namespace {

using MillisecondScheduler = SwingMetro::PeriodicScheduler<5>;
using MicrosecondScheduler = SwingMetro::PeriodicScheduler<1'000>;

static_assert(MillisecondScheduler::period > 0U);
static_assert(MillisecondScheduler::period < (std::uint32_t{1} << 31U));

void test_scheduler_is_inert_before_start() {
    MillisecondScheduler scheduler;

    TEST_ASSERT_FALSE(scheduler.poll(0));
    TEST_ASSERT_FALSE(scheduler.poll(UINT32_MAX));
}

void test_millisecond_scheduler_waits_for_first_deadline() {
    MillisecondScheduler scheduler;
    scheduler.start(100);

    TEST_ASSERT_EQUAL_UINT32(105, scheduler.nextDeadline());
    TEST_ASSERT_FALSE(scheduler.poll(104));
    TEST_ASSERT_TRUE(scheduler.poll(105));
    TEST_ASSERT_EQUAL_UINT32(110, scheduler.nextDeadline());
}

void test_delayed_pass_runs_once_and_resynchronizes() {
    MillisecondScheduler scheduler;
    scheduler.start(0);

    TEST_ASSERT_TRUE(scheduler.poll(17));
    TEST_ASSERT_EQUAL_UINT32(20, scheduler.nextDeadline());
    TEST_ASSERT_FALSE(scheduler.poll(17));
    TEST_ASSERT_TRUE(scheduler.poll(20));
}

void test_millisecond_scheduler_wraps() {
    MillisecondScheduler scheduler;
    scheduler.start(UINT32_MAX - 2U);

    TEST_ASSERT_EQUAL_UINT32(2, scheduler.nextDeadline());
    TEST_ASSERT_FALSE(scheduler.poll(1));
    TEST_ASSERT_TRUE(scheduler.poll(2));
    TEST_ASSERT_EQUAL_UINT32(7, scheduler.nextDeadline());
}

void test_microsecond_scheduler_wraps() {
    MicrosecondScheduler scheduler;
    scheduler.start(UINT32_MAX - 500U);

    TEST_ASSERT_EQUAL_UINT32(499, scheduler.nextDeadline());
    TEST_ASSERT_FALSE(scheduler.poll(498));
    TEST_ASSERT_TRUE(scheduler.poll(499));
    TEST_ASSERT_EQUAL_UINT32(1'499, scheduler.nextDeadline());
}

void test_delayed_pass_resynchronizes_across_wrap() {
    MillisecondScheduler scheduler;
    scheduler.start(UINT32_MAX - 10U);

    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 5U, scheduler.nextDeadline());
    TEST_ASSERT_TRUE(scheduler.poll(8));
    TEST_ASSERT_EQUAL_UINT32(9, scheduler.nextDeadline());
    TEST_ASSERT_FALSE(scheduler.poll(8));
}

void test_schedulers_have_independent_deadlines() {
    MillisecondScheduler matrixScheduler;
    MicrosecondScheduler encoderScheduler;
    matrixScheduler.start(100);
    encoderScheduler.start(100);

    TEST_ASSERT_TRUE(matrixScheduler.poll(105));
    TEST_ASSERT_FALSE(encoderScheduler.poll(105));
    TEST_ASSERT_TRUE(encoderScheduler.poll(1'100));
    TEST_ASSERT_FALSE(matrixScheduler.poll(105));
}

} // namespace

void test_periodic_scheduler_main() {
    RUN_TEST(test_scheduler_is_inert_before_start);
    RUN_TEST(test_millisecond_scheduler_waits_for_first_deadline);
    RUN_TEST(test_delayed_pass_runs_once_and_resynchronizes);
    RUN_TEST(test_millisecond_scheduler_wraps);
    RUN_TEST(test_microsecond_scheduler_wraps);
    RUN_TEST(test_delayed_pass_resynchronizes_across_wrap);
    RUN_TEST(test_schedulers_have_independent_deadlines);
}
