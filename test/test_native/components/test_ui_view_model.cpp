#include <unity.h>

#include "components/ui_view_model.h"

void test_ui_view_model_returns_published_snapshot() {
    UiViewModel viewModel;
    std::bitset<16> notesState;
    notesState.set(0);
    notesState.set(7);
    notesState.set(15);
    viewModel.publish({.tempo = 240, .swing = 66, .volume = 100, .activeNote = 15, .notesState = notesState});

    const UiSettings settings = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(240, settings.tempo);
    TEST_ASSERT_EQUAL_UINT8(66, settings.swing);
    TEST_ASSERT_EQUAL_UINT8(100, settings.volume);
    TEST_ASSERT_EQUAL_UINT8(15, settings.activeNote);
    TEST_ASSERT_TRUE(settings.notesState.test(0));
    TEST_ASSERT_TRUE(settings.notesState.test(7));
    TEST_ASSERT_TRUE(settings.notesState.test(15));
    TEST_ASSERT_FALSE(settings.notesState.test(1));
}

void test_ui_view_model_replaces_the_whole_snapshot() {
    UiViewModel viewModel;
    std::bitset<16> firstNotesState;
    firstNotesState.set(0);
    firstNotesState.set(15);
    viewModel.publish({.tempo = 120, .swing = 50, .volume = 100, .activeNote = 15, .notesState = firstNotesState});

    std::bitset<16> secondNotesState;
    secondNotesState.set(7);
    viewModel.publish({.tempo = 121, .swing = 51, .volume = 99, .activeNote = 7, .notesState = secondNotesState});

    const UiSettings settings = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(121, settings.tempo);
    TEST_ASSERT_EQUAL_UINT8(51, settings.swing);
    TEST_ASSERT_EQUAL_UINT8(99, settings.volume);
    TEST_ASSERT_EQUAL_UINT8(7, settings.activeNote);
    TEST_ASSERT_FALSE(settings.notesState.test(0));
    TEST_ASSERT_TRUE(settings.notesState.test(7));
    TEST_ASSERT_FALSE(settings.notesState.test(15));
}

void test_ui_view_model_preserves_no_active_note() {
    UiViewModel viewModel;
    viewModel.publish({.tempo = 120, .swing = 50, .volume = 100, .activeNote = UINT8_MAX, .notesState = {}});

    const UiSettings settings = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, settings.activeNote);
}

void test_ui_view_model_preserves_all_note_bits() {
    UiViewModel viewModel;
    std::bitset<16> notesState;
    notesState.set();
    viewModel.publish({.tempo = 120, .swing = 50, .volume = 100, .activeNote = 0, .notesState = notesState});

    const UiSettings settings = viewModel.read();
    TEST_ASSERT_EQUAL_HEX16(UINT16_MAX, static_cast<uint16_t>(settings.notesState.to_ulong()));
}

void test_ui_view_model_main() {
    RUN_TEST(test_ui_view_model_returns_published_snapshot);
    RUN_TEST(test_ui_view_model_replaces_the_whole_snapshot);
    RUN_TEST(test_ui_view_model_preserves_no_active_note);
    RUN_TEST(test_ui_view_model_preserves_all_note_bits);
}
