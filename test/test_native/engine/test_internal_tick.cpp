#include "test_internal_tick.h"

#include "engine/internal_tick_source.h"

#include <unity.h>

namespace {

using SwingMetro::InternalTickSource;

void test_interval_residual_is_distributed_at_40_120_and_240_bpm() {
    InternalTickSource source;
    TEST_ASSERT_EQUAL_UINT32(62500, source.start(0, 40));
    TEST_ASSERT_EQUAL_UINT32(62500, source.onAlarm(62500, 62500));

    TEST_ASSERT_EQUAL_UINT32(20833, source.start(0, 120));
    TEST_ASSERT_EQUAL_UINT32(20833, source.onAlarm(20833, 20833));
    TEST_ASSERT_EQUAL_UINT32(20834, source.onAlarm(41666, 41666));

    TEST_ASSERT_EQUAL_UINT32(10416, source.start(0, 240));
    TEST_ASSERT_EQUAL_UINT32(10417, source.onAlarm(10416, 10416));
    TEST_ASSERT_EQUAL_UINT32(10417, source.onAlarm(20833, 20833));
}

void test_callback_timing_tracks_lateness_wrap_and_horizon_rebaseline() {
    InternalTickSource source;
    (void)source.start(0, 120);
    (void)source.onAlarm(1'000, 1'010);
    (void)source.onAlarm(2'000, 2'050);
    (void)source.onAlarm(3'000, 3'020);
    auto diagnostics = source.diagnostics();
    TEST_ASSERT_EQUAL_UINT32(1'040, diagnostics.maxActualCallbackIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(50, diagnostics.maxCallbackLatenessUs);

    (void)source.onAlarm(UINT32_MAX - 20, UINT32_MAX - 10);
    (void)source.onAlarm(20, 30);
    diagnostics = source.diagnostics();
    TEST_ASSERT_EQUAL_UINT32(1'040, diagnostics.maxActualCallbackIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(50, diagnostics.maxCallbackLatenessUs);

    const auto antipodal = 30U + SwingMetro::TIMESTAMP_COMPARISON_HORIZON_US;
    (void)source.onAlarm(antipodal, antipodal);
    (void)source.onAlarm(antipodal + 25U, antipodal + 25U);
    TEST_ASSERT_EQUAL_UINT32(1'040, source.diagnostics().maxActualCallbackIntervalUs);
}

void test_publish_overflow_and_arm_failure_are_accounted_without_extra_records() {
    InternalTickSource source;
    (void)source.start(0, 120);
    for (std::uint32_t callback = 1; callback <= 15; ++callback) {
        (void)source.onAlarm(callback, callback);
    }
    source.onAlarmArmFailure();
    const auto diagnostics = source.diagnostics();
    TEST_ASSERT_EQUAL_UINT32(15, diagnostics.alarmCallbackInvocations);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.synchronousStartPublicationAttempts);
    TEST_ASSERT_EQUAL_UINT32(15, diagnostics.successfulPublications);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.failedPublications);
    TEST_ASSERT_EQUAL_UINT32(1, source.ticks().overflowCount());
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.alarmArmFailures);

    SwingMetro::TransportTickRecord record;
    std::uint32_t publishedRecordCount = 0;
    while (source.ticks().pop(record)) {
        ++publishedRecordCount;
    }
    TEST_ASSERT_EQUAL_UINT32(15, publishedRecordCount);
}

void test_start_rebaselines_callback_interval_after_an_inactive_gap() {
    InternalTickSource source;
    (void)source.start(0, 120);
    (void)source.onAlarm(1'000, 1'000);
    source.stop();
    (void)source.start(1'000'000, 120);
    (void)source.onAlarm(1'020'000, 1'020'000);

    TEST_ASSERT_EQUAL_UINT32(0, source.diagnostics().maxActualCallbackIntervalUs);
}

} // namespace

void test_internal_tick_main() {
    RUN_TEST(test_interval_residual_is_distributed_at_40_120_and_240_bpm);
    RUN_TEST(test_callback_timing_tracks_lateness_wrap_and_horizon_rebaseline);
    RUN_TEST(test_publish_overflow_and_arm_failure_are_accounted_without_extra_records);
    RUN_TEST(test_start_rebaselines_callback_interval_after_an_inactive_gap);
}
