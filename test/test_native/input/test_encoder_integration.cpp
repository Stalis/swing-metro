#include "test_encoder_integration.h"

#include "input/app_event_handler.h"
#include "input/app_input.h"
#include "input/main_display_context.h"
#include <adapters/encoder_input.h>
#include <context_input.h>
#include <cstdint>
#include <unity.h>
#include <utils/counter.h>

namespace {

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
};

template <typename TAdapter>
void routeDirection(IntegrationState& state, const TAdapter& adapter, EncoderDirection direction) {
    const auto input = adapter.translate(direction);
    TEST_ASSERT_TRUE(input.has_value());

    const auto result = state.router.dispatch(*input);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Emitted),
                            static_cast<std::uint8_t>(result.status()));

    state.handler.handle(result.event());
}

void addMainContext(IntegrationState& state) {
    const auto result = state.router.addContext(state.context);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(result));
}

void test_tempo_encoder_full_input_path() {
    IntegrationState state;
    const ContextInput::EncoderInputAdapter<SwingMetro::InputId> adapter{
        SwingMetro::InputId::TempoEncoder,
    };
    addMainContext(state);

    routeDirection(state, adapter, EncoderDirection::Right);

    TEST_ASSERT_EQUAL_UINT8(121, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(121, state.sequencer.getBpm());
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(100, state.volume.getValue());
}

void test_swing_encoder_full_input_path() {
    IntegrationState state;
    const ContextInput::EncoderInputAdapter<SwingMetro::InputId> adapter{
        SwingMetro::InputId::SwingEncoder,
    };
    addMainContext(state);

    routeDirection(state, adapter, EncoderDirection::Right);

    TEST_ASSERT_EQUAL_UINT8(120, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(51, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(100, state.volume.getValue());
}

void test_volume_encoder_full_input_path() {
    IntegrationState state;
    const ContextInput::EncoderInputAdapter<SwingMetro::InputId> adapter{
        SwingMetro::InputId::VolumeEncoder,
    };
    addMainContext(state);

    routeDirection(state, adapter, EncoderDirection::Left);

    TEST_ASSERT_EQUAL_UINT8(120, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(50, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(99, state.volume.getValue());
}

} // namespace

void test_encoder_integration_main() {
    RUN_TEST(test_tempo_encoder_full_input_path);
    RUN_TEST(test_swing_encoder_full_input_path);
    RUN_TEST(test_volume_encoder_full_input_path);
}
