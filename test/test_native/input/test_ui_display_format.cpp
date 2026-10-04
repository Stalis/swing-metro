#include "test_ui_display_format.h"

#include "input/ui_display_format.h"

#include <unity.h>

namespace {

void test_midi_channel_labels() {
    char label[8]{};
    SwingMetro::UiDisplayFormat::midiChannels(0xFFFF, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("ALL", label);
    SwingMetro::UiDisplayFormat::midiChannels(0, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("OFF", label);
    SwingMetro::UiDisplayFormat::midiChannels(0x0001, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("1", label);
    SwingMetro::UiDisplayFormat::midiChannels(0x8000, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("16", label);
    SwingMetro::UiDisplayFormat::midiChannels(0x007F, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("7/16", label);
    SwingMetro::UiDisplayFormat::midiChannels(0x1FFF, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("13/16", label);
    SwingMetro::UiDisplayFormat::midiChannels(0x7FFF, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("15/16", label);
}

void test_slot_window_includes_cancel_and_reaches_last_slot() {
    using SwingMetro::UiDisplayFormat::slotWindowStart;
    TEST_ASSERT_EQUAL_INT(-1, slotWindowStart(SwingMetro::PROGRAM_STORAGE_CANCEL_SLOT));
    TEST_ASSERT_EQUAL_INT(-1, slotWindowStart(0));
    TEST_ASSERT_EQUAL_INT(5, slotWindowStart(7));
    TEST_ASSERT_EQUAL_INT(10, slotWindowStart(15));
    TEST_ASSERT_EQUAL_INT(15, slotWindowStart(15) + 5);
}

} // namespace

void test_ui_display_format_main() {
    RUN_TEST(test_midi_channel_labels);
    RUN_TEST(test_slot_window_includes_cancel_and_reaches_last_slot);
}
