#include "test_app_event_handler.h"

#include "engine/sequencer.h"
#include "input/app_event_handler.h"
#include "input/app_input.h"
#include <cstdint>
#include <unity.h>
#include <utils/counter.h>

namespace {

struct TestState {
    Counter<std::uint8_t> tempo{{.step = 1,
                                 .value = 120,
                                 .minValue = 40,
                                 .maxValue = 240,
                                 .overflowBehavior = CounterOverflowBehavior::Clamp}};
    Counter<std::uint8_t> swing{{.step = 1,
                                 .value = 50,
                                 .minValue = 50,
                                 .maxValue = 100,
                                 .overflowBehavior = CounterOverflowBehavior::Clamp}};
    Counter<std::uint8_t> volume{{.step = 1,
                                  .value = 100,
                                  .minValue = 0,
                                  .maxValue = 100,
                                  .overflowBehavior = CounterOverflowBehavior::Clamp}};
    Sequencer sequencer;
    SwingMetro::AppEventHandler handler{{
        .tempo = tempo,
        .swing = swing,
        .volume = volume,
        .sequencer = sequencer,
    }};
};

void test_app_handler_applies_tempo_delta_and_updates_sequencer() {
    TestState state;

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustTempo{3}});
    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustTempo{-2}});

    TEST_ASSERT_EQUAL_UINT8(121, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(121, state.sequencer.getBpm());
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(100, state.volume.getValue());
}

void test_app_handler_changes_only_swing() {
    TestState state;

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustSwing{4}});

    TEST_ASSERT_EQUAL_UINT8(120, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(54, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(100, state.volume.getValue());
    TEST_ASSERT_EQUAL_UINT8(120, state.sequencer.getBpm());
}

void test_app_handler_changes_only_volume() {
    TestState state;

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustVolume{-5}});

    TEST_ASSERT_EQUAL_UINT8(120, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(95, state.volume.getValue());
    TEST_ASSERT_EQUAL_UINT8(120, state.sequencer.getBpm());
}

void test_app_handler_ignores_zero_delta() {
    TestState state;

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustTempo{0}});
    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustSwing{0}});
    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustVolume{0}});

    TEST_ASSERT_EQUAL_UINT8(120, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(100, state.volume.getValue());
    TEST_ASSERT_EQUAL_UINT8(120, state.sequencer.getBpm());
}

void test_app_handler_preserves_counter_clamps() {
    TestState state;

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustTempo{127}});
    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustSwing{-128}});
    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustVolume{-128}});

    TEST_ASSERT_EQUAL_UINT8(240, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(240, state.sequencer.getBpm());
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(0, state.volume.getValue());

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustTempo{-128}});
    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustSwing{127}});
    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustVolume{127}});

    TEST_ASSERT_EQUAL_UINT8(112, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(112, state.sequencer.getBpm());
    TEST_ASSERT_EQUAL_UINT8(100, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(100, state.volume.getValue());

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::AdjustTempo{-128}});

    TEST_ASSERT_EQUAL_UINT8(40, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(40, state.sequencer.getBpm());
}

void test_app_handler_toggles_valid_step_only() {
    TestState state;

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::ToggleStep{5}});
    TEST_ASSERT_TRUE(state.sequencer.getStepsEnabled().test(5));

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::ToggleStep{5}});
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().test(5));

    state.handler.handle(SwingMetro::AppEvent{SwingMetro::ToggleStep{STEPS_COUNT}});
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().any());
}

} // namespace

void test_app_event_handler_main() {
    RUN_TEST(test_app_handler_applies_tempo_delta_and_updates_sequencer);
    RUN_TEST(test_app_handler_changes_only_swing);
    RUN_TEST(test_app_handler_changes_only_volume);
    RUN_TEST(test_app_handler_ignores_zero_delta);
    RUN_TEST(test_app_handler_preserves_counter_clamps);
    RUN_TEST(test_app_handler_toggles_valid_step_only);
}
