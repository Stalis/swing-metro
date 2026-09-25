#include "test_runtime_timing_diagnostics.h"

#include "engine/runtime_timing_diagnostics.h"

#include <cstdint>
#include <limits>
#include <unity.h>

namespace {

void test_aggregates_inclusive_handler_and_nested_flush_durations() {
    SwingMetro::RuntimeTimingDiagnostics diagnostics;
    diagnostics.recordDisplayFlush(20);
    diagnostics.recordDisplayFlush(30);
    diagnostics.recordLvTimerHandler(100);
    diagnostics.recordLvTimerHandler(80);

    const auto request = diagnostics.requestSnapshot();
    diagnostics.publishRequestedSnapshot();
    SwingMetro::RuntimeTimingSnapshot snapshot;

    TEST_ASSERT_TRUE(diagnostics.readSnapshot(request, snapshot));
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.lvTimerHandlerCount);
    TEST_ASSERT_EQUAL_UINT32(180, snapshot.lvTimerHandlerInclusiveTotalUs);
    TEST_ASSERT_EQUAL_UINT32(100, snapshot.lvTimerHandlerInclusiveMaxUs);
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.displayFlushCount);
    TEST_ASSERT_EQUAL_UINT32(50, snapshot.displayFlushInclusiveTotalUs);
    TEST_ASSERT_EQUAL_UINT32(30, snapshot.displayFlushInclusiveMaxUs);
}

void test_request_publish_read_reset_does_not_double_count() {
    SwingMetro::RuntimeTimingDiagnostics diagnostics;
    diagnostics.recordLvTimerHandler(10);
    const auto firstRequest = diagnostics.requestSnapshot();
    SwingMetro::RuntimeTimingSnapshot snapshot;

    TEST_ASSERT_FALSE(diagnostics.readSnapshot(firstRequest, snapshot));
    diagnostics.publishRequestedSnapshot();
    TEST_ASSERT_TRUE(diagnostics.readSnapshot(firstRequest, snapshot));
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.lvTimerHandlerCount);
    diagnostics.publishRequestedSnapshot();

    diagnostics.recordLvTimerHandler(20);
    diagnostics.recordDisplayFlush(5);
    const auto secondRequest = diagnostics.requestSnapshot();
    diagnostics.publishRequestedSnapshot();
    TEST_ASSERT_FALSE(diagnostics.readSnapshot(firstRequest, snapshot));
    TEST_ASSERT_TRUE(diagnostics.readSnapshot(secondRequest, snapshot));
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.lvTimerHandlerCount);
    TEST_ASSERT_EQUAL_UINT32(20, snapshot.lvTimerHandlerInclusiveTotalUs);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.displayFlushCount);
    TEST_ASSERT_EQUAL_UINT32(5, snapshot.displayFlushInclusiveTotalUs);
}

void test_saturation_and_unsigned_wrap_duration_are_preserved() {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    constexpr std::uint32_t startedAtUs = maximum - 4U;
    constexpr std::uint32_t finishedAtUs = 7U;

    TEST_ASSERT_EQUAL_UINT32(12, finishedAtUs - startedAtUs);
    SwingMetro::RuntimeTimingDiagnostics diagnostics;
    diagnostics.recordLvTimerHandler(maximum);
    diagnostics.recordLvTimerHandler(1);
    diagnostics.recordDisplayFlush(maximum);
    diagnostics.recordDisplayFlush(1);
    const auto request = diagnostics.requestSnapshot();
    diagnostics.publishRequestedSnapshot();
    SwingMetro::RuntimeTimingSnapshot snapshot;

    TEST_ASSERT_TRUE(diagnostics.readSnapshot(request, snapshot));
    TEST_ASSERT_EQUAL_UINT32(maximum, snapshot.lvTimerHandlerInclusiveTotalUs);
    TEST_ASSERT_EQUAL_UINT32(maximum, snapshot.displayFlushInclusiveTotalUs);
    TEST_ASSERT_EQUAL_UINT32(maximum,
                             SwingMetro::RuntimeTimingDiagnostics::saturatingAdd(maximum - 1U, 2));
    TEST_ASSERT_EQUAL_UINT32(maximum,
                             SwingMetro::RuntimeTimingDiagnostics::saturatingIncrement(maximum));
}

} // namespace

void test_runtime_timing_diagnostics_main() {
    RUN_TEST(test_aggregates_inclusive_handler_and_nested_flush_durations);
    RUN_TEST(test_request_publish_read_reset_does_not_double_count);
    RUN_TEST(test_saturation_and_unsigned_wrap_duration_are_preserved);
}
