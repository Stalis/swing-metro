#include "test_internal_tick.h"

#include "engine/internal_tick_source.h"

#include <unity.h>

namespace {

using SwingMetro::InternalTickSource;

void test_absolute_grid_ignores_callback_latency() {
    InternalTickSource source;
    (void)source.start(0, 120);
    const auto first = source.alarmRequest();
    TEST_ASSERT_EQUAL_UINT32(20'833, first.deadlineUs);
    (void)source.onAlarm(first, first.deadlineUs + 10);
    const auto second = source.alarmRequest();
    (void)source.onAlarm(second, second.deadlineUs + 40);
    const auto third = source.alarmRequest();
    (void)source.onAlarm(third, third.deadlineUs + 5);
    TEST_ASSERT_EQUAL_UINT32(41'666, second.deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(62'500, third.deadlineUs);

    SwingMetro::TransportTickRecord record;
    TEST_ASSERT_TRUE(source.ticks().pop(record));
    TEST_ASSERT_EQUAL_UINT32(0, record.timestampUs);
    TEST_ASSERT_TRUE(source.ticks().pop(record));
    TEST_ASSERT_EQUAL_UINT32(first.deadlineUs, record.timestampUs);
    TEST_ASSERT_TRUE(source.ticks().pop(record));
    TEST_ASSERT_EQUAL_UINT32(second.deadlineUs, record.timestampUs);
    TEST_ASSERT_EQUAL_UINT32(40, source.diagnostics().maxCallbackLatenessUs);
}

void test_fractional_grid_is_exact_over_long_run() {
    constexpr std::uint32_t COUNT = 10'000;
    for (const std::uint8_t bpm : {68, 100, 137, 240}) {
        InternalTickSource source;
        SwingMetro::TransportTickRecord record;
        (void)source.start(0, bpm);
        TEST_ASSERT_TRUE(source.ticks().pop(record));
        for (std::uint32_t index = 0; index < COUNT; ++index) {
            const auto request = source.alarmRequest();
            (void)source.onAlarm(request, request.deadlineUs);
            TEST_ASSERT_TRUE(source.ticks().pop(record));
        }
        const auto expected = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(COUNT + 1) * InternalTickSource::MICROSECONDS_PER_MINUTE) /
            (static_cast<std::uint32_t>(bpm) * InternalTickSource::PPQN));
        TEST_ASSERT_EQUAL_UINT32(expected, source.alarmRequest().deadlineUs);
        TEST_ASSERT_EQUAL_UINT32(0, source.diagnostics().failedPublications);
    }
}

void test_grid_wrap_preserves_deadlines() {
    InternalTickSource source;
    constexpr std::uint32_t START = UINT32_MAX - 10'000;
    (void)source.start(START, 240);
    const auto first = source.alarmRequest();
    (void)source.onAlarm(first, first.deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(START + 10'416, first.deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(first.deadlineUs + 10'417, source.alarmRequest().deadlineUs);
}

void test_start_stop_invalidate_requests_and_bpm_preserves_pending_request() {
    InternalTickSource source;
    (void)source.start(1'000, 120);
    const auto old = source.alarmRequest();
    source.stop();
    (void)source.onAlarm(old, old.deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(1, source.diagnostics().staleAlarmCallbacks);
    TEST_ASSERT_EQUAL_UINT32(0, source.diagnostics().successfulPublications -
                                    source.diagnostics().synchronousStartPublicationAttempts);

    (void)source.start(10'000, 120);
    const auto beforeBpm = source.alarmRequest();
    TEST_ASSERT_TRUE(source.updateBpm(100));
    const auto afterBpm = source.alarmRequest();
    TEST_ASSERT_EQUAL_UINT32(beforeBpm.deadlineUs, afterBpm.deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(beforeBpm.generation, afterBpm.generation);
    TEST_ASSERT_EQUAL_UINT32(25'000, source.onAlarm(beforeBpm, beforeBpm.deadlineUs));
    TEST_ASSERT_EQUAL_UINT32(beforeBpm.deadlineUs + 25'000, source.alarmRequest().deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(2, source.diagnostics().synchronousStartPublicationAttempts);
    TEST_ASSERT_EQUAL_UINT32(1, source.diagnostics().staleAlarmCallbacks);
}

void test_rapid_bpm_updates_preserve_pending_boundary_and_apply_latest_period() {
    InternalTickSource source;
    (void)source.start(0, 120);
    const auto initial = source.alarmRequest();

    TEST_ASSERT_TRUE(source.updateBpm(121));
    const auto at121 = source.alarmRequest();
    TEST_ASSERT_TRUE(source.updateBpm(180));
    const auto at180 = source.alarmRequest();
    TEST_ASSERT_TRUE(source.updateBpm(240));
    const auto at240 = source.alarmRequest();

    TEST_ASSERT_EQUAL_UINT32(initial.deadlineUs, at121.deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(initial.deadlineUs, at180.deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(initial.deadlineUs, at240.deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(initial.generation, at121.generation);
    TEST_ASSERT_EQUAL_UINT32(initial.generation, at180.generation);
    TEST_ASSERT_EQUAL_UINT32(initial.generation, at240.generation);

    TEST_ASSERT_EQUAL_UINT32(10'416, source.onAlarm(initial, initial.deadlineUs));
    TEST_ASSERT_EQUAL_UINT32(at240.deadlineUs + 10'416, source.alarmRequest().deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(0, source.diagnostics().staleAlarmCallbacks);
    TEST_ASSERT_EQUAL_UINT32(0, source.diagnostics().missedScheduledTargets);
}

void test_same_and_inactive_bpm_updates_do_not_create_alarm_requests() {
    InternalTickSource source;
    TEST_ASSERT_TRUE(source.updateBpm(180));
    TEST_ASSERT_FALSE(source.alarmRequest().valid());
    TEST_ASSERT_FALSE(source.updateBpm(180));

    TEST_ASSERT_EQUAL_UINT32(13'888, source.start(1'000, 180));
    const auto beforeNoOp = source.alarmRequest();
    TEST_ASSERT_FALSE(source.updateBpm(180));
    const auto afterNoOp = source.alarmRequest();
    TEST_ASSERT_EQUAL_UINT32(beforeNoOp.generation, afterNoOp.generation);
    TEST_ASSERT_EQUAL_UINT32(beforeNoOp.deadlineUs, afterNoOp.deadlineUs);
}

void test_bpm_update_preserves_wrapped_pending_boundary() {
    InternalTickSource source;
    constexpr std::uint32_t START = UINT32_MAX - 10'000;
    (void)source.start(START, 120);
    const auto beforeUpdate = source.alarmRequest();
    TEST_ASSERT_TRUE(source.updateBpm(240));
    const auto afterUpdate = source.alarmRequest();
    TEST_ASSERT_EQUAL_UINT32(beforeUpdate.deadlineUs, afterUpdate.deadlineUs);
    TEST_ASSERT_EQUAL_UINT32(10'416, source.onAlarm(afterUpdate, afterUpdate.deadlineUs));
    TEST_ASSERT_EQUAL_UINT32(afterUpdate.deadlineUs + 10'416, source.alarmRequest().deadlineUs);
}

void test_overdue_equality_skips_due_targets_without_a_burst() {
    InternalTickSource source;
    (void)source.start(0, 120);
    const auto first = source.alarmRequest();
    (void)source.onAlarm(first, first.deadlineUs);
    const auto second = source.alarmRequest();
    (void)source.onAlarm(second, second.deadlineUs + 62'500);
    TEST_ASSERT_EQUAL_UINT32(3, source.diagnostics().successfulPublications);
    TEST_ASSERT_EQUAL_UINT32(3, source.diagnostics().missedScheduledTargets);
    TEST_ASSERT_FALSE(
        SwingMetro::timestampReached(second.deadlineUs + 62'500, source.alarmRequest().deadlineUs));
}

void test_out_of_horizon_callback_deactivates_grid() {
    InternalTickSource source;
    (void)source.start(0, 120);
    const auto request = source.alarmRequest();
    (void)source.onAlarm(request, request.deadlineUs + SwingMetro::TIMESTAMP_COMPARISON_HORIZON_US);
    TEST_ASSERT_FALSE(source.alarmRequest().valid());
    TEST_ASSERT_EQUAL_UINT32(1, source.diagnostics().outOfHorizonAlarmCallbacks);
}

void test_overflow_and_arm_failures_are_accounted_by_request() {
    InternalTickSource source;
    (void)source.start(0, 120);
    for (std::uint32_t index = 0; index < 15; ++index) {
        const auto request = source.alarmRequest();
        (void)source.onAlarm(request, request.deadlineUs);
    }
    TEST_ASSERT_EQUAL_UINT32(1, source.ticks().overflowCount());
    TEST_ASSERT_EQUAL_UINT32(1, source.diagnostics().failedPublications);

    const auto old = source.alarmRequest();
    source.stop();
    (void)source.start(old.deadlineUs, 100);
    const auto current = source.alarmRequest();
    source.onAlarmArmFailure(old);
    TEST_ASSERT_TRUE(source.alarmRequest().valid());
    source.onAlarmArmFailure(current);
    TEST_ASSERT_FALSE(source.alarmRequest().valid());
    TEST_ASSERT_EQUAL_UINT32(2, source.diagnostics().alarmArmFailures);
    TEST_ASSERT_EQUAL_UINT32(1, source.diagnostics().staleAlarmArmFailures);
}

void test_callback_metrics_use_actual_time_but_processing_uses_grid_time() {
    InternalTickSource source;
    (void)source.start(0, 120);
    auto request = source.alarmRequest();
    (void)source.onAlarm(request, request.deadlineUs + 10);
    request = source.alarmRequest();
    (void)source.onAlarm(request, request.deadlineUs + 50);
    TEST_ASSERT_EQUAL_UINT32(20'873, source.diagnostics().maxActualCallbackIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(50, source.diagnostics().maxCallbackLatenessUs);
}

void test_callback_interval_includes_stale_callbacks_and_rebaselines_at_horizon() {
    InternalTickSource source;
    (void)source.start(UINT32_MAX - 20'000, 240);
    const auto request = source.alarmRequest();
    const auto firstActual = request.deadlineUs + 10;
    (void)source.onAlarm(request, firstActual);

    const SwingMetro::InternalTickAlarmRequest stale{};
    const auto staleActual = firstActual + 1'040;
    (void)source.onAlarm(stale, staleActual);
    auto diagnostics = source.diagnostics();
    TEST_ASSERT_EQUAL_UINT32(1'040, diagnostics.maxActualCallbackIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(10, diagnostics.maxCallbackLatenessUs);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.staleAlarmCallbacks);

    const auto antipodal = staleActual + SwingMetro::TIMESTAMP_COMPARISON_HORIZON_US;
    (void)source.onAlarm(stale, antipodal);
    (void)source.onAlarm(stale, antipodal + 25U);
    diagnostics = source.diagnostics();
    TEST_ASSERT_EQUAL_UINT32(1'040, diagnostics.maxActualCallbackIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(3, diagnostics.staleAlarmCallbacks);
}

} // namespace

void test_internal_tick_main() {
    RUN_TEST(test_absolute_grid_ignores_callback_latency);
    RUN_TEST(test_fractional_grid_is_exact_over_long_run);
    RUN_TEST(test_grid_wrap_preserves_deadlines);
    RUN_TEST(test_start_stop_invalidate_requests_and_bpm_preserves_pending_request);
    RUN_TEST(test_rapid_bpm_updates_preserve_pending_boundary_and_apply_latest_period);
    RUN_TEST(test_same_and_inactive_bpm_updates_do_not_create_alarm_requests);
    RUN_TEST(test_bpm_update_preserves_wrapped_pending_boundary);
    RUN_TEST(test_overdue_equality_skips_due_targets_without_a_burst);
    RUN_TEST(test_out_of_horizon_callback_deactivates_grid);
    RUN_TEST(test_overflow_and_arm_failures_are_accounted_by_request);
    RUN_TEST(test_callback_metrics_use_actual_time_but_processing_uses_grid_time);
    RUN_TEST(test_callback_interval_includes_stale_callbacks_and_rebaselines_at_horizon);
}
