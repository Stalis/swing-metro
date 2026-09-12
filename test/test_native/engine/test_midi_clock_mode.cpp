#include "test_midi_clock_mode.h"

#include "engine/midi_clock_mode.h"

#include <cstdint>
#include <unity.h>

namespace {

void test_default_mode_is_off() {
    const SwingMetro::MidiClockSettings settings;

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(settings.mode()));
}

void test_apply_each_mode_and_reapply() {
    SwingMetro::MidiClockSettings settings;

    settings.apply(SwingMetro::MidiClockMode::Internal);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Internal),
                            static_cast<std::uint8_t>(settings.mode()));
    settings.apply(SwingMetro::MidiClockMode::External);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(settings.mode()));
    settings.apply(SwingMetro::MidiClockMode::Off);
    settings.apply(SwingMetro::MidiClockMode::Off);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(settings.mode()));
}

} // namespace

void test_midi_clock_mode_main() {
    RUN_TEST(test_default_mode_is_off);
    RUN_TEST(test_apply_each_mode_and_reapply);
}
