#include "test_midi_clock_modal.h"

#include "input/midi_clock_modal.h"

#include <cstdint>
#include <unity.h>

namespace {

void test_modal_opens_at_active_mode_and_clamps_preview() {
    SwingMetro::MidiClockModal modal;
    modal.open(SwingMetro::MidiClockMode::Internal);
    TEST_ASSERT_TRUE(modal.isOpen());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMenuItem::Internal),
                            static_cast<std::uint8_t>(modal.selection()));

    modal.adjustPreview(-127);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMenuItem::Off),
                            static_cast<std::uint8_t>(modal.selection()));
    modal.adjustPreview(127);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMenuItem::Cancel),
                            static_cast<std::uint8_t>(modal.selection()));
}

void test_modal_confirms_modes_but_not_cancel() {
    SwingMetro::MidiClockModal modal;
    TEST_ASSERT_FALSE(modal.confirmedMode().has_value());

    modal.open(SwingMetro::MidiClockMode::Off);
    modal.adjustPreview(2);
    TEST_ASSERT_TRUE(modal.confirmedMode().has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(*modal.confirmedMode()));

    modal.adjustPreview(1);
    TEST_ASSERT_FALSE(modal.confirmedMode().has_value());
    modal.close();
    TEST_ASSERT_FALSE(modal.isOpen());
}

} // namespace

void test_midi_clock_modal_main() {
    RUN_TEST(test_modal_opens_at_active_mode_and_clamps_preview);
    RUN_TEST(test_modal_confirms_modes_but_not_cancel);
}
