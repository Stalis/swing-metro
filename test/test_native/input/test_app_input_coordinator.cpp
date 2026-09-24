#include "test_app_input_coordinator.h"

#include "components/ui_view_model.h"
#include "engine/midi_clock_mode.h"
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
                                 .maxValue = SwingMetro::SWING_MAX_VALUE,
                                 .overflowBehavior = CounterOverflowBehavior::Clamp}};
    Counter<std::uint8_t> volume{{.step = 1,
                                  .value = 100,
                                  .minValue = 0,
                                  .maxValue = 100,
                                  .overflowBehavior = CounterOverflowBehavior::Clamp}};
    Sequencer sequencer;
    SwingMetro::MidiClockSettings midiClock;
    SwingMetro::AppEventHandler handler{{tempo, swing, volume, sequencer}};
    SwingMetro::AppInputCoordinator<Capacity> coordinator{handler, sequencer, midiClock};
    SwingMetro::StepButtonInputs buttons;
    ContextInput::ButtonInputAdapter<SwingMetro::InputId> tempoSwitch{{
        SwingMetro::InputId::TempoSwitch,
        500,
    }};
    ContextInput::ButtonInputAdapter<SwingMetro::InputId> volumeSwitch{{
        SwingMetro::InputId::VolumeEncoder,
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

template <std::size_t Capacity>
auto longPressTempo(State<Capacity>& state, std::uint32_t pressedAt) -> void {
    routeBatch(state, state.tempoSwitch.onPressed(pressedAt));
    routeBatch(state, state.tempoSwitch.update(pressedAt + 500));
    routeBatch(state, state.tempoSwitch.onReleased(pressedAt + 600));
}

template <std::size_t Capacity>
auto clickTempo(State<Capacity>& state, std::uint32_t pressedAt) -> void {
    routeBatch(state, state.tempoSwitch.onPressed(pressedAt));
    routeBatch(state, state.tempoSwitch.onReleased(pressedAt + 100));
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
    TEST_ASSERT_EQUAL_UINT8(127, openUi.selectedVelocity);

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

    const auto velocityEvent = turn(state, SwingMetro::InputId::SwingEncoder, -1);
    TEST_ASSERT_TRUE(velocityEvent.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::AdjustVelocity>(*velocityEvent));
    const auto gateEvent = turn(state, SwingMetro::InputId::VolumeEncoder, -1);
    TEST_ASSERT_TRUE(gateEvent.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::AdjustGate>(*gateEvent));
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(100, state.volume.getValue());
    TEST_ASSERT_EQUAL_UINT8(126, *state.sequencer.getStepVelocity(3));
    TEST_ASSERT_EQUAL_UINT8(99, *state.sequencer.getStepGate(3));

    click(state, 3, 1000);
    TEST_ASSERT_FALSE(state.sequencer.getStepsEnabled().test(3));
    TEST_ASSERT_EQUAL_UINT8(3, *state.coordinator.selectedStep());
    longPress(state, 3, 2000);
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(122, state.tempo.getValue());
    const auto volumeEvent = turn(state, SwingMetro::InputId::VolumeEncoder, -1);
    TEST_ASSERT_TRUE(volumeEvent.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::AdjustVolume>(*volumeEvent));
    TEST_ASSERT_EQUAL_UINT8(99, state.volume.getValue());
}

void test_shift_changes_note_by_octaves_only_while_editing_step() {
    State state;
    setShift(state, true);
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(121, state.tempo.getValue());
    turn(state, SwingMetro::InputId::SwingEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(51, state.swing.getValue());

    longPress(state, 0, 100);
    TEST_ASSERT_EQUAL_UINT32(4, state.coordinator.stackSize());
    const auto octaveEvent = turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_TRUE(octaveEvent.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::AdjustNote>(*octaveEvent));
    TEST_ASSERT_EQUAL_INT16(12, std::get<SwingMetro::AdjustNote>(*octaveEvent).delta);
    TEST_ASSERT_EQUAL_UINT8(48, *state.sequencer.getStepMidiNote(0));

    turn(state, SwingMetro::InputId::TempoEncoder, -1);
    TEST_ASSERT_EQUAL_UINT8(36, *state.sequencer.getStepMidiNote(0));
    turn(state, SwingMetro::InputId::SwingEncoder, -1);
    TEST_ASSERT_EQUAL_UINT8(126, *state.sequencer.getStepVelocity(0));
    TEST_ASSERT_EQUAL_UINT8(51, state.swing.getValue());

    setShift(state, false);
    const auto semitoneEvent = turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_TRUE(semitoneEvent.has_value());
    TEST_ASSERT_EQUAL_INT16(1, std::get<SwingMetro::AdjustNote>(*semitoneEvent).delta);
    TEST_ASSERT_EQUAL_UINT8(37, *state.sequencer.getStepMidiNote(0));
    longPress(state, 0, 1000);
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(122, state.tempo.getValue());
}

void test_shift_note_clamps_and_survives_step_navigation() {
    State state;
    longPress(state, 0, 100);
    setShift(state, true);
    turn(state, SwingMetro::InputId::TempoEncoder, 127);
    TEST_ASSERT_EQUAL_UINT8(127, *state.sequencer.getStepMidiNote(0));
    turn(state, SwingMetro::InputId::TempoEncoder, -128);
    TEST_ASSERT_EQUAL_UINT8(36, *state.sequencer.getStepMidiNote(0));

    click(state, 3, 1000);
    TEST_ASSERT_EQUAL_UINT8(3, *state.coordinator.selectedStep());
    TEST_ASSERT_EQUAL_UINT32(4, state.coordinator.stackSize());
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(48, *state.sequencer.getStepMidiNote(3));
    TEST_ASSERT_EQUAL_UINT8(36, *state.sequencer.getStepMidiNote(0));

    longPress(state, 3, 2000);
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    TEST_ASSERT_TRUE(state.coordinator.isShiftActive());
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(121, state.tempo.getValue());
    setShift(state, false);
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
}

void test_volume_hold_keeps_shift_in_step_settings_without_opening_storage() {
    State state;
    longPress(state, 0, 100);
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());

    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.volumeSwitch.onPressed(1'000)));
    TEST_ASSERT_TRUE(state.coordinator.isShiftActive());
    TEST_ASSERT_EQUAL_UINT32(4, state.coordinator.stackSize());
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(48, *state.sequencer.getStepMidiNote(0));

    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.volumeSwitch.update(1'500)));
    TEST_ASSERT_TRUE(state.coordinator.isShiftActive());
    TEST_ASSERT_FALSE(state.coordinator.isProgramStorageModalOpen());
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_EQUAL_UINT32(4, state.coordinator.stackSize());

    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.volumeSwitch.onReleased(1'600)));
    TEST_ASSERT_FALSE(state.coordinator.isShiftActive());
    TEST_ASSERT_FALSE(state.coordinator.isProgramStorageModalOpen());
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(49, *state.sequencer.getStepMidiNote(0));
}

void test_volume_long_press_still_opens_storage_from_main_display() {
    State state;
    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.volumeSwitch.onPressed(100)));
    TEST_ASSERT_TRUE(state.coordinator.isShiftActive());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());

    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.volumeSwitch.update(600)));
    TEST_ASSERT_TRUE(state.coordinator.isProgramStorageModalOpen());
    TEST_ASSERT_FALSE(state.coordinator.isShiftActive());
    TEST_ASSERT_FALSE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());

    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.volumeSwitch.onReleased(700)));
    TEST_ASSERT_TRUE(state.coordinator.isProgramStorageModalOpen());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
}

void test_velocity_changes_only_selected_step_and_ui_snapshot() {
    State state;
    UiViewModel viewModel;
    longPress(state, 0, 100);
    turn(state, SwingMetro::InputId::SwingEncoder, -2);
    TEST_ASSERT_EQUAL_UINT8(125, *state.sequencer.getStepVelocity(0));
    TEST_ASSERT_EQUAL_UINT8(127, *state.sequencer.getStepVelocity(3));
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    TEST_ASSERT_EQUAL_UINT8(125, viewModel.read().selectedVelocity);

    click(state, 3, 1000);
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    TEST_ASSERT_EQUAL_UINT8(3, viewModel.read().selectedStep);
    TEST_ASSERT_EQUAL_UINT8(127, viewModel.read().selectedVelocity);
    turn(state, SwingMetro::InputId::SwingEncoder, -1);
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    TEST_ASSERT_EQUAL_UINT8(126, viewModel.read().selectedVelocity);
    TEST_ASSERT_EQUAL_UINT8(125, *state.sequencer.getStepVelocity(0));
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());
}

void test_gate_clamps_changes_only_selected_step_and_publishes_snapshot() {
    State state;
    UiViewModel viewModel;
    longPress(state, 0, 100);
    turn(state, SwingMetro::InputId::VolumeEncoder, -127);
    TEST_ASSERT_EQUAL_UINT8(STEP_MIN_GATE, *state.sequencer.getStepGate(0));
    turn(state, SwingMetro::InputId::VolumeEncoder, 127);
    TEST_ASSERT_EQUAL_UINT8(STEP_MAX_GATE, *state.sequencer.getStepGate(0));

    click(state, 3, 1000);
    turn(state, SwingMetro::InputId::VolumeEncoder, -1);
    TEST_ASSERT_EQUAL_UINT8(99, *state.sequencer.getStepGate(3));
    TEST_ASSERT_EQUAL_UINT8(STEP_MAX_GATE, *state.sequencer.getStepGate(0));
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    TEST_ASSERT_EQUAL_UINT8(3, viewModel.read().selectedStep);
    TEST_ASSERT_EQUAL_UINT8(99, viewModel.read().selectedGate);
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

void test_tempo_switch_click_toggles_transport_once_and_long_press_does_not() {
    State state;
    state.sequencer.toggleRunning(0);
    state.sequencer.sync(0);
    TEST_ASSERT_TRUE(state.sequencer.isRunning());

    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.tempoSwitch.onPressed(100)));
    TEST_ASSERT_TRUE(state.sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.tempoSwitch.onReleased(200)));
    TEST_ASSERT_FALSE(state.sequencer.isRunning());
    TEST_ASSERT_FALSE(state.sequencer.update(1000000));

    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.tempoSwitch.onPressed(1000)));
    TEST_ASSERT_EQUAL_UINT32(1, routeBatch(state, state.tempoSwitch.update(1500)));
    TEST_ASSERT_TRUE(state.coordinator.isMidiClockModalOpen());
    TEST_ASSERT_EQUAL_UINT32(3, state.coordinator.stackSize());
    TEST_ASSERT_EQUAL_UINT32(0, routeBatch(state, state.tempoSwitch.onReleased(1600)));
    TEST_ASSERT_FALSE(state.sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(state.midiClock.mode()));
}

void test_midi_clock_modal_clamps_confirms_and_publishes_snapshot() {
    State state;
    UiViewModel viewModel;
    longPressTempo(state, 100);
    TEST_ASSERT_TRUE(state.coordinator.isMidiClockModalOpen());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(state.coordinator.midiClockPreviewMode()));

    turn(state, SwingMetro::InputId::TempoEncoder, -1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(state.coordinator.midiClockPreviewMode()));
    turn(state, SwingMetro::InputId::TempoEncoder, 2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(state.coordinator.midiClockPreviewMode()));
    turn(state, SwingMetro::InputId::TempoEncoder, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(state.coordinator.midiClockPreviewMode()));
    TEST_ASSERT_EQUAL_UINT8(120, state.tempo.getValue());
    TEST_ASSERT_FALSE(turn(state, SwingMetro::InputId::SwingEncoder, 1).has_value());
    TEST_ASSERT_FALSE(turn(state, SwingMetro::InputId::VolumeEncoder, 1).has_value());
    TEST_ASSERT_EQUAL_UINT8(STEP_DEFAULT_GATE, *state.sequencer.getStepGate(0));
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());

    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    const auto openUi = viewModel.read();
    TEST_ASSERT_TRUE(openUi.midiClockModalOpen);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(openUi.midiClockActive));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(openUi.midiClockPreview));

    clickTempo(state, 1000);
    TEST_ASSERT_FALSE(state.coordinator.isMidiClockModalOpen());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(state.midiClock.mode()));
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    const auto closedUi = viewModel.read();
    TEST_ASSERT_FALSE(closedUi.midiClockModalOpen);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(closedUi.midiClockActive));
}

void test_midi_clock_modal_preserves_step_settings_and_restores_shift() {
    State state;
    state.sequencer.toggleRunning(0);
    longPress(state, 2, 100);
    setShift(state, true);
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_TRUE(state.coordinator.isShiftActive());
    TEST_ASSERT_EQUAL_UINT32(4, state.coordinator.stackSize());

    longPressTempo(state, 1000);
    TEST_ASSERT_TRUE(state.coordinator.isMidiClockModalOpen());
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_FALSE(state.coordinator.isShiftActive());
    TEST_ASSERT_EQUAL_UINT32(4, state.coordinator.stackSize());
    TEST_ASSERT_FALSE(turn(state, SwingMetro::InputId::SwingEncoder, -1).has_value());
    TEST_ASSERT_EQUAL_UINT8(127, *state.sequencer.getStepVelocity(2));

    clickTempo(state, 2000);
    TEST_ASSERT_FALSE(state.coordinator.isMidiClockModalOpen());
    TEST_ASSERT_TRUE(state.coordinator.hasStepSettingsContext());
    TEST_ASSERT_TRUE(state.sequencer.isRunning());
    TEST_ASSERT_TRUE(state.coordinator.isShiftActive());
    TEST_ASSERT_EQUAL_UINT32(4, state.coordinator.stackSize());
}

void test_restart_runs_first_step_immediately_and_keeps_sixteenth_grid() {
    State state;
    UiViewModel viewModel;
    state.sequencer.toggleRunning(0);
    state.sequencer.sync(0);
    TEST_ASSERT_FALSE(state.sequencer.getDisplayStepIndex().has_value());
    TEST_ASSERT_TRUE(state.sequencer.update(0));
    TEST_ASSERT_EQUAL_UINT8(0, *state.sequencer.getDisplayStepIndex());
    TEST_ASSERT_FALSE(state.sequencer.update(124999));
    TEST_ASSERT_TRUE(state.sequencer.update(125000));
    TEST_ASSERT_EQUAL_UINT8(1, *state.sequencer.getDisplayStepIndex());

    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::ToggleTransport{}}, nullptr,
                                     200000);
    TEST_ASSERT_FALSE(state.sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT8(1, *state.sequencer.getDisplayStepIndex());

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

    TEST_ASSERT_TRUE(state.sequencer.update(300000));
    TEST_ASSERT_EQUAL_UINT8(0, *state.sequencer.getDisplayStepIndex());
    TEST_ASSERT_FALSE(state.sequencer.update(424999));
    TEST_ASSERT_TRUE(state.sequencer.update(425000));
    TEST_ASSERT_EQUAL_UINT8(1, *state.sequencer.getDisplayStepIndex());
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

void test_velocity_clamps_and_current_step_uses_edited_value() {
    State state;
    TEST_ASSERT_FALSE(state.sequencer.getStepVelocity(STEPS_COUNT).has_value());
    TEST_ASSERT_FALSE(state.sequencer.adjustStepVelocity(STEPS_COUNT, -1));
    TEST_ASSERT_EQUAL_UINT8(127, *state.sequencer.getStepVelocity(0));

    TEST_ASSERT_TRUE(state.sequencer.adjustStepVelocity(0, -128));
    TEST_ASSERT_EQUAL_UINT8(1, *state.sequencer.getStepVelocity(0));
    TEST_ASSERT_TRUE(state.sequencer.adjustStepVelocity(0, -1));
    TEST_ASSERT_EQUAL_UINT8(1, *state.sequencer.getStepVelocity(0));
    TEST_ASSERT_EQUAL_UINT8(127, *state.sequencer.getStepVelocity(1));
    TEST_ASSERT_TRUE(state.sequencer.adjustStepVelocity(0, 127));
    TEST_ASSERT_EQUAL_UINT8(127, *state.sequencer.getStepVelocity(0));
    TEST_ASSERT_TRUE(state.sequencer.adjustStepVelocity(0, -7));
    state.sequencer.toggleRunning(0);
    state.sequencer.sync(0);
    TEST_ASSERT_TRUE(state.sequencer.update(125000));
    TEST_ASSERT_EQUAL_UINT8(0, state.sequencer.getCurrentStepIndex());
    TEST_ASSERT_EQUAL_UINT8(120, state.sequencer.currentStepVelocity());
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
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::AdjustVelocity{-1}}, nullptr,
                                     1000);
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::AdjustGate{-1}}, nullptr,
                                     1000);
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::DeactivateShift{}}, nullptr,
                                     1000);
    TEST_ASSERT_EQUAL_UINT32(2, state.coordinator.stackSize());
    TEST_ASSERT_FALSE(state.coordinator.selectedStep().has_value());
    TEST_ASSERT_FALSE(state.coordinator.isShiftActive());
    TEST_ASSERT_EQUAL_UINT8(36, *state.sequencer.getStepMidiNote(0));
    TEST_ASSERT_EQUAL_UINT8(127, *state.sequencer.getStepVelocity(0));
    TEST_ASSERT_EQUAL_UINT8(STEP_DEFAULT_GATE, *state.sequencer.getStepGate(0));
}

void test_apply_midi_clock_mode_changes_only_settings() {
    State state;
    const auto bpm = state.sequencer.getBpm();
    const auto running = state.sequencer.isRunning();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(state.coordinator.midiClockMode()));

    state.coordinator.handleAppEvent(
        SwingMetro::AppEvent{SwingMetro::ApplyMidiClockMode{SwingMetro::MidiClockMode::Internal}},
        nullptr, 1000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Internal),
                            static_cast<std::uint8_t>(state.midiClock.mode()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Internal),
                            static_cast<std::uint8_t>(state.coordinator.midiClockMode()));
    TEST_ASSERT_EQUAL_UINT8(bpm, state.sequencer.getBpm());
    TEST_ASSERT_EQUAL(running, state.sequencer.isRunning());

    state.coordinator.handleAppEvent(
        SwingMetro::AppEvent{SwingMetro::ApplyMidiClockMode{SwingMetro::MidiClockMode::External}},
        nullptr, 1000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(state.midiClock.mode()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(state.coordinator.midiClockMode()));
}

void test_external_mode_ignores_local_tempo_and_transport() {
    State state;
    state.sequencer.toggleRunning(0);
    state.coordinator.handleAppEvent(
        SwingMetro::AppEvent{SwingMetro::ApplyMidiClockMode{SwingMetro::MidiClockMode::External}},
        nullptr, 1000);
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::AdjustTempo{5}}, nullptr,
                                     1000);
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::ToggleTransport{}}, nullptr,
                                     1000);
    TEST_ASSERT_EQUAL_UINT8(120, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(120, state.sequencer.getBpm());
    TEST_ASSERT_TRUE(state.sequencer.isRunning());
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

void test_program_storage_modal_blocks_input_and_publishes_result() {
    State state;
    UiViewModel viewModel;
    state.sequencer.stop();
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::OpenProgramStorage{}},
                                     nullptr, 1000);
    TEST_ASSERT_TRUE(state.coordinator.isProgramStorageModalOpen());
    TEST_ASSERT_FALSE(turn(state, SwingMetro::InputId::TempoEncoder, 1).has_value());
    const auto volumeEvent = turn(state, SwingMetro::InputId::VolumeEncoder, 1);
    TEST_ASSERT_TRUE(volumeEvent.has_value());
    TEST_ASSERT_FALSE(std::holds_alternative<SwingMetro::AdjustGate>(*volumeEvent));
    TEST_ASSERT_EQUAL_UINT8(120, state.tempo.getValue());

    state.coordinator.handleAppEvent(
        SwingMetro::AppEvent{SwingMetro::SelectProgramStorageAction{1}}, nullptr, 1000);
    state.coordinator.handleAppEvent(
        SwingMetro::AppEvent{SwingMetro::ConfirmProgramStorageAction{}}, nullptr, 1000);
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::SelectProgramStorageSlot{4}},
                                     nullptr, 1000);
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::ConfirmProgramStorageSlot{}},
                                     nullptr, 1000);
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageModalState::Busy),
                            static_cast<std::uint8_t>(viewModel.read().programStorageState));
    state.coordinator.processProgramStorage();
    viewModel.publish(state.coordinator.decorateUiSettings({120, 50, 100}));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageModalState::Error),
                            static_cast<std::uint8_t>(viewModel.read().programStorageState));
    state.coordinator.handleAppEvent(SwingMetro::AppEvent{SwingMetro::CloseProgramStorage{}},
                                     nullptr, 1000);
    TEST_ASSERT_FALSE(state.coordinator.isProgramStorageModalOpen());
    TEST_ASSERT_FALSE(state.sequencer.isRunning());
}

} // namespace

void test_app_input_coordinator_main() {
    RUN_TEST(test_open_switch_close_and_publish_ui);
    RUN_TEST(test_encoder_changes_note_in_settings_and_tempo_after_close);
    RUN_TEST(test_shift_changes_note_by_octaves_only_while_editing_step);
    RUN_TEST(test_shift_note_clamps_and_survives_step_navigation);
    RUN_TEST(test_volume_hold_keeps_shift_in_step_settings_without_opening_storage);
    RUN_TEST(test_volume_long_press_still_opens_storage_from_main_display);
    RUN_TEST(test_velocity_changes_only_selected_step_and_ui_snapshot);
    RUN_TEST(test_gate_clamps_changes_only_selected_step_and_publishes_snapshot);
    RUN_TEST(test_click_in_settings_selects_step_without_toggling_it);
    RUN_TEST(test_settings_reselects_on_press_without_adding_context_and_releases_on_close);
    RUN_TEST(test_navigation_failure_preserves_main_page);
    RUN_TEST(test_gesture_capture_suppresses_remaining_phases_only_for_source);
    RUN_TEST(test_shift_lifecycle_is_independent_of_navigation);
    RUN_TEST(test_tempo_switch_click_toggles_transport_once_and_long_press_does_not);
    RUN_TEST(test_midi_clock_modal_clamps_confirms_and_publishes_snapshot);
    RUN_TEST(test_midi_clock_modal_preserves_step_settings_and_restores_shift);
    RUN_TEST(test_restart_runs_first_step_immediately_and_keeps_sixteenth_grid);
    RUN_TEST(test_note_clamps_and_invalid_index);
    RUN_TEST(test_velocity_clamps_and_current_step_uses_edited_value);
    RUN_TEST(test_unknown_input_and_mismatched_payload_do_not_change_app_state);
    RUN_TEST(test_inactive_navigation_and_note_events_are_no_ops);
    RUN_TEST(test_apply_midi_clock_mode_changes_only_settings);
    RUN_TEST(test_external_mode_ignores_local_tempo_and_transport);
    RUN_TEST(test_repeated_fast_navigation_keeps_only_one_settings_context);
    RUN_TEST(test_program_storage_modal_blocks_input_and_publishes_result);
}
