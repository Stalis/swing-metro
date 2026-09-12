#include "test_step_button_integration.h"

#include "input/app_event_handler.h"
#include "input/app_input.h"
#include "input/main_display_context.h"
#include "input/pad_button_ids.h"
#include "input/step_button_inputs.h"
#include <array>
#include <context_input.h>
#include <cstddef>
#include <cstdint>
#include <unity.h>
#include <utils/counter.h>
#include <variant>

namespace {

static_assert(SwingMetro::StepButtonInputs::longPressThresholdMs == 500);
static_assert(SwingMetro::PadButtonIds::values.size() == STEPS_COUNT);

struct IntegrationState {
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
    SwingMetro::MainDisplayContext context;
    ContextInput::Router<SwingMetro::InputEvent, SwingMetro::AppEvent, 1> router;
    SwingMetro::AppEventHandler handler{{
        .tempo = tempo,
        .swing = swing,
        .volume = volume,
        .sequencer = sequencer,
    }};
    SwingMetro::StepButtonInputs buttons;
};

auto routeBatch(IntegrationState& state, const SwingMetro::StepButtonInputs::Batch& batch)
    -> std::size_t {
    std::size_t emitted = 0;
    for (std::size_t index = 0; index < batch.size(); ++index) {
        const auto result = state.router.dispatch(batch[index]);
        if (result.hasEvent()) {
            state.handler.handle(result.event());
            ++emitted;
        }
    }
    return emitted;
}

void addMainContext(IntegrationState& state) {
    const auto result = state.router.addContext(state.context);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(result));
}

void test_physical_layout_is_the_expected_permutation() {
    constexpr std::array<std::uint8_t, STEPS_COUNT> expected = {
        0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15,
    };
    std::array<bool, STEPS_COUNT> seen{};

    for (std::size_t physical = 0; physical < STEPS_COUNT; ++physical) {
        const auto step = SwingMetro::PadButtonIds::values[physical];
        TEST_ASSERT_EQUAL_UINT8(expected[physical], step);
        TEST_ASSERT_TRUE(step < STEPS_COUNT);
        TEST_ASSERT_FALSE(seen[step]);
        seen[step] = true;

        const auto inputId = SwingMetro::inputIdForStep(step);
        const auto decodedStep = SwingMetro::stepIndexFromInputId(inputId);
        TEST_ASSERT_TRUE(decodedStep.has_value());
        TEST_ASSERT_EQUAL_UINT8(step, *decodedStep);
    }

    for (const auto found : seen) {
        TEST_ASSERT_TRUE(found);
    }

    TEST_ASSERT_FALSE(
        SwingMetro::stepIndexFromInputId(SwingMetro::InputId::TempoEncoder).has_value());
    TEST_ASSERT_FALSE(
        SwingMetro::stepIndexFromInputId(static_cast<SwingMetro::InputId>(0xFF)).has_value());
}

void test_each_step_click_toggles_exactly_once() {
    IntegrationState state;
    addMainContext(state);

    for (std::uint8_t step = 0; step < STEPS_COUNT; ++step) {
        const auto pressed = state.buttons.onPressed(step, 100);
        const auto released = state.buttons.onReleased(step, 200);

        TEST_ASSERT_EQUAL_UINT32(1, pressed.size());
        TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::inputIdForStep(step)),
                                static_cast<std::uint8_t>(pressed[0].source));
        TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, pressed));
        TEST_ASSERT_EQUAL_UINT32(2, released.size());
        TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, released));
        TEST_ASSERT_TRUE(state.sequencer.getStepsEnabled().test(step));
        TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(step + 1),
                                 state.sequencer.getStepsEnabled().count());
    }

    TEST_ASSERT_FALSE(state.handler.takeOpenStepSettingsRequest().has_value());
}

void test_each_step_long_press_requests_settings_without_toggle() {
    IntegrationState state;
    addMainContext(state);

    for (std::uint8_t step = 0; step < STEPS_COUNT; ++step) {
        const auto pressed = state.buttons.onPressed(step, 100);
        const auto waiting = state.buttons.update(step, 599);
        const auto longPressed = state.buttons.update(step, 600);
        const auto repeated = state.buttons.update(step, 700);
        const auto released = state.buttons.onReleased(step, 800);

        TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, pressed));
        TEST_ASSERT_TRUE(waiting.empty());
        TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, longPressed));
        TEST_ASSERT_TRUE(repeated.empty());
        TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, released));
        TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().any());

        const auto request = state.handler.takeOpenStepSettingsRequest();
        TEST_ASSERT_TRUE(request.has_value());
        TEST_ASSERT_EQUAL_UINT8(step, *request);
    }
}

void test_two_held_steps_have_independent_timers() {
    IntegrationState state;
    addMainContext(state);

    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onPressed(1, 100)));
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onPressed(8, 350)));
    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.buttons.update(1, 600)));
    TEST_ASSERT_TRUE(state.buttons.update(8, 600).empty());
    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.buttons.update(8, 850)));
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onReleased(1, 900)));
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onReleased(8, 900)));
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().any());

    const auto request = state.handler.takeOpenStepSettingsRequest();
    TEST_ASSERT_TRUE(request.has_value());
    TEST_ASSERT_EQUAL_UINT8(8, *request);
}

void test_simultaneous_clicks_create_two_step_actions() {
    IntegrationState state;
    addMainContext(state);
    const auto firstStep = SwingMetro::PadButtonIds::values[0];
    const auto secondStep = SwingMetro::PadButtonIds::values[2];

    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onPressed(firstStep, 100)));
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onPressed(secondStep, 100)));
    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.buttons.onReleased(firstStep, 200)));
    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.buttons.onReleased(secondStep, 200)));

    TEST_ASSERT_TRUE(state.sequencer.getStepsEnabled().test(0));
    TEST_ASSERT_TRUE(state.sequencer.getStepsEnabled().test(8));
    TEST_ASSERT_EQUAL_UINT32(2, state.sequencer.getStepsEnabled().count());
}

void test_late_release_without_update_does_not_toggle_step() {
    IntegrationState state;
    addMainContext(state);

    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onPressed(5, 100)));
    const auto released = state.buttons.onReleased(5, 601);
    TEST_ASSERT_EQUAL_UINT32(2, released.size());
    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, released));
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().any());

    const auto request = state.handler.takeOpenStepSettingsRequest();
    TEST_ASSERT_TRUE(request.has_value());
    TEST_ASSERT_EQUAL_UINT8(5, *request);
}

void test_invalid_step_is_ignored_before_array_access() {
    SwingMetro::StepButtonInputs buttons;

    TEST_ASSERT_TRUE(buttons.onPressed(STEPS_COUNT, 100).empty());
    TEST_ASSERT_TRUE(buttons.update(0xFF, 600).empty());
    TEST_ASSERT_TRUE(buttons.onReleased(STEPS_COUNT, 700).empty());
}

} // namespace

void test_step_button_integration_main() {
    RUN_TEST(test_physical_layout_is_the_expected_permutation);
    RUN_TEST(test_each_step_click_toggles_exactly_once);
    RUN_TEST(test_each_step_long_press_requests_settings_without_toggle);
    RUN_TEST(test_two_held_steps_have_independent_timers);
    RUN_TEST(test_simultaneous_clicks_create_two_step_actions);
    RUN_TEST(test_late_release_without_update_does_not_toggle_step);
    RUN_TEST(test_invalid_step_is_ignored_before_array_access);
}
