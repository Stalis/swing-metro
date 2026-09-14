#include "test_internal_tick.h"

#include "engine/internal_tick_source.h"

#include <unity.h>

namespace {

using SwingMetro::InternalTickSource;

void test_interval_residual_is_distributed_at_40_120_and_240_bpm() {
    InternalTickSource source;
    TEST_ASSERT_EQUAL_UINT32(62500, source.start(0, 40));
    TEST_ASSERT_EQUAL_UINT32(62500, source.onAlarm(62500));

    TEST_ASSERT_EQUAL_UINT32(20833, source.start(0, 120));
    TEST_ASSERT_EQUAL_UINT32(20833, source.onAlarm(20833));
    TEST_ASSERT_EQUAL_UINT32(20834, source.onAlarm(41666));

    TEST_ASSERT_EQUAL_UINT32(10416, source.start(0, 240));
    TEST_ASSERT_EQUAL_UINT32(10417, source.onAlarm(10416));
    TEST_ASSERT_EQUAL_UINT32(10417, source.onAlarm(20833));
}

} // namespace

void test_internal_tick_main() {
    RUN_TEST(test_interval_residual_is_distributed_at_40_120_and_240_bpm);
}
