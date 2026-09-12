#include "test_app_input_coordinator.h"

#include "components/ui_view_model.h"
#include "input/app_input_coordinator.h"
#include "input/step_button_inputs.h"

#include <adapters/button_input.h>
#include <adapters/trigger_input.h>
#include <cstddef>
#include <cstdint>
#include <unity.h>
#include <utils/counter.h>
#include <variant>

namespace {

template <std::size_t Capacity = 8>
struct State {
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
    SwingMetro::AppEventHandler handler{{tempo, swing, volume, sequencer}};
    SwingMetro::AppInputCoordinator<Capacity> coordinator{handler, sequencer};
    SwingMetro::StepButtonInputs buttons;
    ContextInput::ButtonInputAdapter<SwingMetro::InputId> tempoSwitch{{
        SwingMetro::InputId::TempoSwitch,
        500,
    }};
    ContextInput::TriggerInputAdapter<SwingMetro::InputId> shift{
        SwingMetro::InputId::ShiftSwitch,
    };
};

template <std::size_t Capacity>
auto routeBatch(State<Capacity>& state, const SwingMetro::StepButtonInputs::Batch& batch)
    -> std::size_t {
    std::size_t emitted = 0;
    for (std::size_t index = 0; index < batch.size(); ++index) {
        if (state.coordinator.dispatch(batch[index], 1000).has_value()) {
            ++emitted;
        }
    }
    return emitted;
}

template <std::size_t Capacity>
auto longPress(State<Capacity>& state, std::uint8_t step, std::uint32_t pressedAt) -> void {
    routeBatch(state, state.buttons.onPressed(step, pressedAt));
    routeBatch(state, state.buttons.update(step, pressedAt + 500));
    routeBatch(state, state.buttons.onReleased(step, pressedAt + 600));
}

template <std::size_t Capacity>
auto click(State<Capacity>& state, std::uint8_t step, std::uint32_t pressedAt) -> void {
    routeBatch(state, state.buttons.onPressed(step, pressedAt));
    routeBatch(state, state.buttons.onReleased(step, pressedAt + 100));
}

template <std::size_t Capacity>
auto turn(State<Capacity>& state, SwingMetro::InputId source, std::int8_t delta)
    -> std::optional<SwingMetro::AppEvent> {
    return state.coordinator.dispatch({source, ContextInput::EncoderInput{delta}}, 1000);
}

template <std::size_t Capacity>
auto setShift(State<Capacity>& state, bool active) -> void {
    const auto input = state.shift.set(active);
    TEST_ASSERT_TRUE(input.has_value());
    state.coordinator.dispatch(*input, 1000);
}

void test_open_switch_close_and_publish_ui() {
    State state;
    UiViewModel viewModel;
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());

    longPress(state, 5, 100);
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_EQUAL_UINT8(5, *state.coordinator.selectedStep());

    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    const auto openUi = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiPage::StepSettings),
                            static_cast<std::uint8_t>(openUi.page));
    TEST_ASSERT_EQUAL_UINT8(5, openUi.selectedStep);
    TEST_ASSERT_EQUAL_UINT8(36, openUi.selectedNote);

    longPress(state, 8, 1000);
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    TEST_ASSERT_EQUAL_UINT8(8, *state.coordinator.selectedStep());

    longPress(state, 8, 2000);
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
    TEST_ASSERT_FALSE(state.coordinator.selectedStep().has_value());
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    const auto closedUi = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiPage::MainDisplay),
                            static_cast<std::uint8_t>(closedUi.page));
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, closedUi.selectedStep);
}

void test_encoder_changes_note_in_settings_and_tempo_after_close() {
    State state;
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(121, state.tempo.getValue());

    longPress(state, 3, 100);
    const auto noteEvent = turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_TRUE(noteEvent.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::AdjustNote>(*noteEvent));
    TEST_ASSERT_EQUAL_UINT8(37, *state.sequencer.getStepMidiNote(3));
    TEST_ASSERT_EQUAL_UINT8(121, state.tempo.getValue());

    TEST_ASSERT_FALSE(turn(state, SwingMetro::InputId::SwingEncoder, 1).has_value());
    TEST_ASSERT_FALSE(turn(state, SwingMetro::InputId::VolumeEncoder, -1).has_value());
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(100, state.volume.getValue());

    click(state, 3, 1000);
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().test(3));
    TEST_ASSERT_EQUAL_UINT8(3, *state.coordinator.selectedStep());
    longPress(state, 3, 2000);
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(122, state.tempo.getValue());
}

void test_click_in_settings_selects_step_without_toggling_it() {
    State state;
    UiViewModel viewModel;

    longPress(state, 0, 100);
    click(state, 2, 1000);
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    TEST_ASSERT_EQUAL_UINT8(2, *state.coordinator.selectedStep());
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().test(0));
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().test(2));

    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    const auto selectedUi = viewModel.read();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiPage::StepSettings),
                            static_cast<std::uint8_t>(selectedUi.page));
    TEST_ASSERT_EQUAL_UINT8(2, selectedUi.selectedStep);

    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(36, *state.sequencer.getStepMidiNote(0));
    TEST_ASSERT_EQUAL_UINT8(37, *state.sequencer.getStepMidiNote(2));

    longPress(state, 0, 2000);
    TEST_ASSERT_EQUAL_UINT8(0, *state.coordinator.selectedStep());
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().test(2));
    longPress(state, 0, 3000);
    TEST_ASSERT_FALSE(state.coordinator.selectedStep().has_value());
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().test(2));

    click(state, 2, 4000);
    TEST_ASSERT_TRUE(state.sequencer.getStepsEnabled().test(2));
}

void test_settings_reselects_on_press_without_adding_context_and_releases_on_close() {
    State state;
    UiViewModel viewModel;

    longPress(state, 0, 100);
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());

    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.buttons.onPressed(3, 1000)));
    TEST_ASSERT_EQUAL_UINT8(3, *state.coordinator.selectedStep());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    TEST_ASSERT_EQUAL_UINT8(3, viewModel.read().selectedStep);
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.update(3, 1500)));
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onReleased(3, 1600)));
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());

    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.buttons.onPressed(7, 2000)));
    TEST_ASSERT_EQUAL_UINT8(7, *state.coordinator.selectedStep());
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onReleased(7, 2100)));
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().test(7));

    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.buttons.onPressed(0, 3000)));
    TEST_ASSERT_EQUAL_UINT8(0, *state.coordinator.selectedStep());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.update(0, 3500)));
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onReleased(0, 3600)));
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());

    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onPressed(0, 4000)));
    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.buttons.update(0, 4500)));
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
    TEST_ASSERT_FALSE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_FALSE(state.coordinator.selectedStep().has_value());
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onReleased(0, 4600)));
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiPage::MainDisplay),
                            static_cast<std::uint8_t>(viewModel.read().page));
}

void test_navigation_failure_preserves_main_page() {
    State<2> state;
    longPress(state, 5, 100);
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
    TEST_ASSERT_FALSE(state.coordinator.selectedStep().has_value());
    TEST_ASSERT_FALSE(state.coordinator.hasStepSettingsContext());

    const SwingMetro::AppEvent invalid{SwingMetro::OpenStepSettings{STEPS_COUNT}};
    state.coordinator.handleAppEvent(invalid, nullptr, 1000);
    TEST_ASSERT_FALSE(state.coordinator.selectedStep().has_value());
}

void test_gesture_capture_suppresses_remaining_phases_only_for_source() {
    State state;
    routeBatch(state, state.buttons.onPressed(5, 100));
    routeBatch(state, state.buttons.onPressed(8, 500));
    routeBatch(state, state.buttons.update(5, 600));
    TEST_ASSERT_EQUAL_UINT8(5, *state.coordinator.selectedStep());

    const auto otherClick = routeBatch(state, state.buttons.onReleased(8, 650));
    TEST_ASSERT_EQUAL_UINT32(1, otherClick);
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().test(8));
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.buttons.onReleased(5, 700)));
    TEST_ASSERT_EQUAL_UINT8(8, *state.coordinator.selectedStep());

    longPress(state, 5, 1000);
    TEST_ASSERT_EQUAL_UINT8(5, *state.coordinator.selectedStep());
    longPress(state, 5, 2000);
    TEST_ASSERT_FALSE(state.coordinator.selectedStep().has_value());

    const SwingMetro::InputEvent pressed{
        SwingMetro::inputIdForStep(2),
        ContextInput::ButtonInput{ContextInput::ButtonPhase::Pressed}};
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::OpenStepSettings{2}},
                                     &pressed, 1000);
    TEST_ASSERT_EQUAL_UINT8(2, *state.coordinator.selectedStep());
    TEST_ASSERT_FALSE(
        state.coordinator
            .dispatch({SwingMetro::inputIdForStep(2),
                       ContextInput::ButtonInput{ContextInput::ButtonPhase::LongPressed}},
                      1000)
            .has_value());
    TEST_ASSERT_FALSE(
        state.coordinator
            .dispatch({SwingMetro::inputIdForStep(2),
                       ContextInput::ButtonInput{ContextInput::ButtonPhase::Released}},
                      1000)
            .has_value());
    TEST_ASSERT_EQUAL_UINT8(2, *state.coordinator.selectedStep());
}

void test_shift_lifecycle_is_independent_of_navigation() {
    State state;
    setShift(state, true);
    TEST_ASSERT_TRUE(state.coordinator.isShiftActive());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());

    longPress(state, 4, 100);
    TEST_ASSERT_TRUE(state.coordinator.isShiftActive());
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_EQUAL_UINT32(4, state.coordinator.stackSize());

    setShift(state, false);
    TEST_ASSERT_FALSE(state.coordinator.isShiftActive());
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());

    setShift(state, true);
    longPress(state, 4, 1000);
    TEST_ASSERT_FALSE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_TRUE(state.coordinator.isShiftActive());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    setShift(state, false);
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
}

void test_tempo_switch_toggles_transport_on_press_in_all_contexts() {
    State state;
    state.sequencer.sync(0);
    TEST_ASSERT_TRUE(state.sequencer.isRunning());

    routeBatch(state, state.tempoSwitch.onPressed(100));
    TEST_ASSERT_FALSE(state.sequencer.isRunning());
    TEST_ASSERT_FALSE(state.sequencer.update(1000000));
    routeBatch(state, state.tempoSwitch.update(600));
    routeBatch(state, state.tempoSwitch.onReleased(700));
    TEST_ASSERT_FALSE(state.sequencer.isRunning());

    longPress(state, 1, 1000);
    setShift(state, true);
    routeBatch(state, state.tempoSwitch.onPressed(2000));
    TEST_ASSERT_TRUE(state.sequencer.isRunning());
    TEST_ASSERT_FALSE(state.sequencer.update(1000));
    TEST_ASSERT_TRUE(state.sequencer.update(126000));
    TEST_ASSERT_EQUAL_UINT8(0, state.sequencer.getCurrentStepIndex());
    routeBatch(state, state.tempoSwitch.onReleased(2100));
    TEST_ASSERT_TRUE(state.sequencer.isRunning());
}

void test_display_has_no_active_step_until_first_tick_after_restart() {
    State state;
    UiViewModel viewModel;
    state.sequencer.sync(0);
    TEST_ASSERT_FALSE(state.sequencer.getDisplayStepIndex().has_value());
    TEST_ASSERT_TRUE(state.sequencer.update(125000));
    TEST_ASSERT_EQUAL_UINT8(0, *state.sequencer.getDisplayStepIndex());

    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::ToggleTransport{}}, nullptr,
                                     200000);
    TEST_ASSERT_FALSE(state.sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT8(0, *state.sequencer.getDisplayStepIndex());

    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::ToggleTransport{}}, nullptr,
                                     300000);
    TEST_ASSERT_TRUE(state.sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT8(STEPS_COUNT - 1, state.sequencer.getCurrentStepIndex());
    TEST_ASSERT_FALSE(state.sequencer.getDisplayStepIndex().has_value());

    viewModel.publish(state.coordinator.decorateUiSettings(
        {.tempo = 120,
         .swing = 50,
         .volume = 100,
         .activeNote = state.sequencer.getDisplayStepIndex().value_or(UINT8_MAX)}));
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, viewModel.read().activeNote);

    TEST_ASSERT_FALSE(state.sequencer.update(424999));
    TEST_ASSERT_FALSE(state.sequencer.getDisplayStepIndex().has_value());
    TEST_ASSERT_TRUE(state.sequencer.update(425000));
    TEST_ASSERT_EQUAL_UINT8(0, *state.sequencer.getDisplayStepIndex());
}

void test_note_clamps_and_invalid_index() {
    State state;
    TEST_ASSERT_FALSE(state.sequencer.getStepMidiNote(STEPS_COUNT).has_value());
    TEST_ASSERT_FALSE(state.sequencer.adjustStepNote(STEPS_COUNT, 1));
    TEST_ASSERT_TRUE(state.sequencer.adjustStepNote(0, -128));
    TEST_ASSERT_EQUAL_UINT8(36, *state.sequencer.getStepMidiNote(0));
    TEST_ASSERT_TRUE(state.sequencer.adjustStepNote(0, 127));
    TEST_ASSERT_EQUAL_UINT8(127, *state.sequencer.getStepMidiNote(0));
    TEST_ASSERT_TRUE(state.sequencer.adjustStepNote(0, 127));
    TEST_ASSERT_EQUAL_UINT8(127, *state.sequencer.getStepMidiNote(0));
}

void test_unknown_input_and_mismatched_payload_do_not_change_app_state() {
    State state;
    constexpr auto unknown = static_cast<SwingMetro::InputId>(0xFF);

    TEST_ASSERT_FALSE(
        state.coordinator.dispatch({unknown, ContextInput::EncoderInput{1}}, 1000).has_value());
    TEST_ASSERT_FALSE(state.coordinator
                          .dispatch({SwingMetro::InputId::TempoEncoder,
                                     ContextInput::ButtonInput{ContextInput::ButtonPhase::Clicked}},
                                    1000)
                          .has_value());
    TEST_ASSERT_FALSE(
        state.coordinator
            .dispatch({SwingMetro::inputIdForStep(3), ContextInput::EncoderInput{1}}, 1000)
            .has_value());
    TEST_ASSERT_EQUAL_UINT8(120, state.tempo.getValue());
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().any());
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());

    longPress(state, 3, 2000);
    TEST_ASSERT_FALSE(
        state.coordinator
            .dispatch({unknown, ContextInput::ButtonInput{ContextInput::ButtonPhase::LongPressed}},
                      3000)
            .has_value());
    TEST_ASSERT_FALSE(state.coordinator
                          .dispatch({SwingMetro::InputId::TempoEncoder,
                                     ContextInput::ButtonInput{ContextInput::ButtonPhase::Pressed}},
                                    3000)
                          .has_value());
    TEST_ASSERT_EQUAL_UINT8(3, *state.coordinator.selectedStep());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
}

void test_inactive_navigation_and_note_events_are_no_ops() {
    State state;
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::CloseStepSettings{}}, nullptr,
                                     1000);
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::AdjustNote{1}}, nullptr,
                                     1000);
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::DeactivateShift{}}, nullptr,
                                     1000);
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
    TEST_ASSERT_FALSE(state.coordinator.selectedStep().has_value());
    TEST_ASSERT_FALSE(state.coordinator.isShiftActive());
    TEST_ASSERT_EQUAL_UINT8(36, *state.sequencer.getStepMidiNote(0));
}

void test_repeated_fast_navigation_keeps_only_one_settings_context() {
    State state;
    for (std::uint8_t step = 0; step < STEPS_COUNT; ++step) {
        const auto next = static_cast<std::uint8_t>((step + 1) % STEPS_COUNT);
        const auto startedAt = static_cast<std::uint32_t>(step) * 3000 + 100;

        longPress(state, step, startedAt);
        TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
        TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());

        click(state, next, startedAt + 1000);
        TEST_ASSERT_EQUAL_UINT8(next, *state.coordinator.selectedStep());
        TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());

        longPress(state, next, startedAt + 2000);
        TEST_ASSERT_FALSE(state.coordinator.hasStepSettingsContext());
        TEST_ASSERT_FALSE(state.coordinator.selectedStep().has_value());
        TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
    }
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().any());
}

} // namespace

void test_app_input_coordinator_main() {
    RUN_TEST(test_open_switch_close_and_publish_ui);
    RUN_TEST(test_encoder_changes_note_in_settings_and_tempo_after_close);
    RUN_TEST(test_click_in_settings_selects_step_without_toggling_it);
    RUN_TEST(test_settings_reselects_on_press_without_adding_context_and_releases_on_close);
    RUN_TEST(test_navigation_failure_preserves_main_page);
    RUN_TEST(test_gesture_capture_suppresses_remaining_phases_only_for_source);
    RUN_TEST(test_shift_lifecycle_is_independent_of_navigation);
    RUN_TEST(test_tempo_switch_toggles_transport_on_press_in_all_contexts);
    RUN_TEST(test_display_has_no_active_step_until_first_tick_after_restart);
    RUN_TEST(test_note_clamps_and_invalid_index);
    RUN_TEST(test_unknown_input_and_mismatched_payload_do_not_change_app_state);
    RUN_TEST(test_inactive_navigation_and_note_events_are_no_ops);
    RUN_TEST(test_repeated_fast_navigation_keeps_only_one_settings_context);
}
