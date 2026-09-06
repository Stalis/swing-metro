#include <unity.h>

#include "components/ui_view_model.h"

void test_ui_view_model_returns_published_snapshot() {
    UiViewModel viewModel;
    viewModel.publish({.tempo = 240, .swing = 66, .volume = 100});

    const UiSettings settings = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(240, settings.tempo);
    TEST_ASSERT_EQUAL_UINT8(66, settings.swing);
    TEST_ASSERT_EQUAL_UINT8(100, settings.volume);
}

void test_ui_view_model_replaces_the_whole_snapshot() {
    UiViewModel viewModel;
    viewModel.publish({.tempo = 120, .swing = 50, .volume = 100});
    viewModel.publish({.tempo = 121, .swing = 51, .volume = 99});

    const UiSettings settings = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(121, settings.tempo);
    TEST_ASSERT_EQUAL_UINT8(51, settings.swing);
    TEST_ASSERT_EQUAL_UINT8(99, settings.volume);
}

void test_ui_view_model_main() {
    RUN_TEST(test_ui_view_model_returns_published_snapshot);
    RUN_TEST(test_ui_view_model_replaces_the_whole_snapshot);
}
