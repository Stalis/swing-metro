#include <thread>
#include <unity.h>

#include "components/ui_view_model.h"

void test_ui_view_model_returns_published_snapshot() {
    UiViewModel viewModel;
    std::bitset<16> notesState;
    notesState.set(0);
    notesState.set(7);
    notesState.set(15);
    viewModel.publish(
        {.tempo = 240, .swing = 66, .volume = 100, .activeNote = 15, .notesState = notesState});

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
    viewModel.publish({.tempo = 120,
                       .swing = 50,
                       .volume = 100,
                       .activeNote = 15,
                       .notesState = firstNotesState});

    std::bitset<16> secondNotesState;
    secondNotesState.set(7);
    viewModel.publish(
        {.tempo = 121, .swing = 51, .volume = 99, .activeNote = 7, .notesState = secondNotesState});

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
    viewModel.publish(
        {.tempo = 120, .swing = 50, .volume = 100, .activeNote = UINT8_MAX, .notesState = {}});

    const UiSettings settings = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, settings.activeNote);
}

void test_ui_view_model_preserves_all_note_bits() {
    UiViewModel viewModel;
    std::bitset<16> notesState;
    notesState.set();
    viewModel.publish(
        {.tempo = 120, .swing = 50, .volume = 100, .activeNote = 0, .notesState = notesState});

    const UiSettings settings = viewModel.read();
    TEST_ASSERT_EQUAL_HEX16(UINT16_MAX, static_cast<uint16_t>(settings.notesState.to_ulong()));
}

void test_ui_view_model_keeps_navigation_fields_together() {
    UiViewModel viewModel;
    viewModel.publish({.tempo = 120,
                       .swing = 50,
                       .volume = 100,
                       .activeNote = 7,
                       .notesState = {},
                       .page = UiPage::StepSettings,
                       .selectedStep = 7,
                       .selectedNote = 49,
                       .transportRunning = false,
                       .shiftActive = true});

    const UiSettings settings = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(UiPage::StepSettings),
                            static_cast<uint8_t>(settings.page));
    TEST_ASSERT_EQUAL_UINT8(7, settings.selectedStep);
    TEST_ASSERT_EQUAL_UINT8(49, settings.selectedNote);
    TEST_ASSERT_FALSE(settings.transportRunning);
    TEST_ASSERT_TRUE(settings.shiftActive);
}

void test_ui_view_model_reads_complete_concurrent_snapshots() {
    UiViewModel viewModel;
    const UiSettings first{.tempo = 120,
                           .swing = 50,
                           .volume = 100,
                           .activeNote = 1,
                           .notesState = std::bitset<16>(0x0001),
                           .page = UiPage::MainDisplay,
                           .selectedStep = UINT8_MAX,
                           .selectedNote = 36,
                           .transportRunning = true,
                           .shiftActive = false};
    const UiSettings second{.tempo = 180,
                            .swing = 75,
                            .volume = 25,
                            .activeNote = 12,
                            .notesState = std::bitset<16>(0xF000),
                            .page = UiPage::StepSettings,
                            .selectedStep = 12,
                            .selectedNote = 61,
                            .transportRunning = false,
                            .shiftActive = true};
    viewModel.publish(first);

    std::thread writer([&viewModel, &first, &second]() {
        for (int index = 0; index < 20000; ++index) {
            viewModel.publish(index % 2 == 0 ? second : first);
        }
    });

    bool coherent = true;
    for (int index = 0; index < 20000; ++index) {
        const auto value = viewModel.read();
        const bool matchesFirst =
            value.tempo == first.tempo && value.swing == first.swing &&
            value.volume == first.volume && value.activeNote == first.activeNote &&
            value.notesState == first.notesState && value.page == first.page &&
            value.selectedStep == first.selectedStep && value.selectedNote == first.selectedNote &&
            value.transportRunning == first.transportRunning &&
            value.shiftActive == first.shiftActive;
        const bool matchesSecond =
            value.tempo == second.tempo && value.swing == second.swing &&
            value.volume == second.volume && value.activeNote == second.activeNote &&
            value.notesState == second.notesState && value.page == second.page &&
            value.selectedStep == second.selectedStep &&
            value.selectedNote == second.selectedNote &&
            value.transportRunning == second.transportRunning &&
            value.shiftActive == second.shiftActive;
        if (!matchesFirst && !matchesSecond) {
            coherent = false;
            break;
        }
    }
    writer.join();
    TEST_ASSERT_TRUE(coherent);
}

void test_ui_view_model_main() {
    RUN_TEST(test_ui_view_model_returns_published_snapshot);
    RUN_TEST(test_ui_view_model_replaces_the_whole_snapshot);
    RUN_TEST(test_ui_view_model_preserves_no_active_note);
    RUN_TEST(test_ui_view_model_preserves_all_note_bits);
    RUN_TEST(test_ui_view_model_keeps_navigation_fields_together);
    RUN_TEST(test_ui_view_model_reads_complete_concurrent_snapshots);
}
