#include "test_encoder_sample_diagnostics.h"

#include "input/encoder_sample_diagnostics.h"

#include <cstdint>
#include <limits>
#include <unity.h>

namespace {

void test_first_sample_and_zero_timestamp_do_not_create_intervals() {
    SwingMetro::EncoderSampleDiagnostics diagnostics;

    diagnostics.recordSample(0);

    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.maxActualIntervalUs());
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.intervalsAboveBound());
}

void test_tracks_maximum_and_strict_bound_crossings() {
    SwingMetro::EncoderSampleDiagnostics diagnostics;

    diagnostics.recordSample(100);
    diagnostics.recordSample(1'000);
    diagnostics.recordSample(2'250);
    diagnostics.recordSample(3'550);
    diagnostics.recordSample(5'000);

    TEST_ASSERT_EQUAL_UINT32(1'450, diagnostics.maxActualIntervalUs());
    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.intervalsAboveBound());
}

void test_interval_equal_to_bound_does_not_cross_it() {
    SwingMetro::EncoderSampleDiagnostics diagnostics;

    diagnostics.recordSample(0);
    diagnostics.recordSample(SwingMetro::EncoderSampleDiagnostics::INTERVAL_BOUND_US);

    TEST_ASSERT_EQUAL_UINT32(SwingMetro::EncoderSampleDiagnostics::INTERVAL_BOUND_US,
                             diagnostics.maxActualIntervalUs());
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.intervalsAboveBound());
}

void test_wraparound_and_uint32_max_interval_are_preserved() {
    SwingMetro::EncoderSampleDiagnostics diagnostics;

    diagnostics.recordSample(1);
    diagnostics.recordSample(0);

    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, diagnostics.maxActualIntervalUs());
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.intervalsAboveBound());

    SwingMetro::EncoderSampleDiagnostics wrapping;
    wrapping.recordSample(UINT32_MAX - 99U);
    wrapping.recordSample(200);
    TEST_ASSERT_EQUAL_UINT32(300, wrapping.maxActualIntervalUs());
    TEST_ASSERT_EQUAL_UINT32(0, wrapping.intervalsAboveBound());
}

void test_saturating_counter_seam_does_not_wrap() {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();

    TEST_ASSERT_EQUAL_UINT32(maximum,
                             SwingMetro::EncoderSampleDiagnostics::saturatingIncrement(maximum));
    TEST_ASSERT_EQUAL_UINT32(
        maximum, SwingMetro::EncoderSampleDiagnostics::saturatingIncrement(maximum - 1U));
}

void test_window_snapshot_resets_without_changing_boot_cumulative_statistics() {
    SwingMetro::EncoderSampleDiagnostics diagnostics;
    diagnostics.recordSample(100);
    diagnostics.recordSample(1'500);
    diagnostics.recordSample(3'000);

    const auto firstWindow = diagnostics.snapshotAndResetWindow();
    TEST_ASSERT_EQUAL_UINT32(1'500, firstWindow.maxActualIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(2, firstWindow.intervalsAboveBound);
    TEST_ASSERT_EQUAL_UINT32(1'500, diagnostics.maxActualIntervalUs());
    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.intervalsAboveBound());

    diagnostics.recordSample(3'900);
    const auto secondWindow = diagnostics.snapshotAndResetWindow();
    TEST_ASSERT_EQUAL_UINT32(900, secondWindow.maxActualIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(0, secondWindow.intervalsAboveBound);
}

} // namespace

void test_encoder_sample_diagnostics_main() {
    RUN_TEST(test_first_sample_and_zero_timestamp_do_not_create_intervals);
    RUN_TEST(test_tracks_maximum_and_strict_bound_crossings);
    RUN_TEST(test_interval_equal_to_bound_does_not_cross_it);
    RUN_TEST(test_wraparound_and_uint32_max_interval_are_preserved);
    RUN_TEST(test_saturating_counter_seam_does_not_wrap);
    RUN_TEST(test_window_snapshot_resets_without_changing_boot_cumulative_statistics);
}
