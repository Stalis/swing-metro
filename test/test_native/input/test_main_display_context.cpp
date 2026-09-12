#include "test_main_display_context.h"

#include "input/app_input.h"
#include "input/main_display_context.h"
#include <context_input.h>
#include <cstdint>
#include <type_traits>
#include <unity.h>
#include <variant>

namespace {

using Result = ContextInput::DispatchResult<SwingMetro::AppEvent>;

static_assert(std::is_empty_v<SwingMetro::MainDisplayContext>);

auto makeEncoderInput(SwingMetro::InputId source, std::int8_t delta) -> SwingMetro::InputEvent {
    return SwingMetro::InputEvent{source, ContextInput::EncoderInput{delta}};
}

auto makeButtonInput(SwingMetro::InputId source, ContextInput::ButtonPhase phase)
    -> SwingMetro::InputEvent {
    return SwingMetro::InputEvent{source, ContextInput::ButtonInput{phase}};
}

void test_main_context_maps_tempo_encoder_with_signed_delta() {
    const SwingMetro::MainDisplayContext context;

    const auto positive = context.handle(makeEncoderInput(SwingMetro::InputId::TempoEncoder, 3));
    const auto negative = context.handle(makeEncoderInput(SwingMetro::InputId::TempoEncoder, -2));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Emitted),
                            static_cast<std::uint8_t>(positive.status()));
    TEST_ASSERT_EQUAL_INT8(3, std::get<SwingMetro::AdjustTempo>(positive.event()).delta);
    TEST_ASSERT_EQUAL_INT8(-2, std::get<SwingMetro::AdjustTempo>(negative.event()).delta);
}

void test_main_context_maps_swing_encoder() {
    const SwingMetro::MainDisplayContext context;

    const auto result = context.handle(makeEncoderInput(SwingMetro::InputId::SwingEncoder, -1));

    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::AdjustSwing>(result.event()));
    TEST_ASSERT_EQUAL_INT8(-1, std::get<SwingMetro::AdjustSwing>(result.event()).delta);
}

void test_main_context_maps_volume_encoder() {
    const SwingMetro::MainDisplayContext context;

    const auto result = context.handle(makeEncoderInput(SwingMetro::InputId::VolumeEncoder, 1));

    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::AdjustVolume>(result.event()));
    TEST_ASSERT_EQUAL_INT8(1, std::get<SwingMetro::AdjustVolume>(result.event()).delta);
}

void test_main_context_passes_non_encoder_payload() {
    const SwingMetro::MainDisplayContext context;
    const SwingMetro::InputEvent input{
        SwingMetro::InputId::TempoEncoder,
        ContextInput::ButtonInput{ContextInput::ButtonPhase::Pressed},
    };

    const auto result = context.handle(input);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Unhandled),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_FALSE(result.hasEvent());
}

void test_main_context_passes_unknown_source() {
    const SwingMetro::MainDisplayContext context;
    const auto unknown = static_cast<SwingMetro::InputId>(0xFF);

    const auto result = context.handle(makeEncoderInput(unknown, 1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Unhandled),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_FALSE(result.hasEvent());
}

void test_main_context_works_through_router() {
    SwingMetro::MainDisplayContext context;
    ContextInput::Router<SwingMetro::InputEvent, SwingMetro::AppEvent, 1> router;
    const auto added = router.addContext(context);

    const auto result = router.dispatch(makeEncoderInput(SwingMetro::InputId::VolumeEncoder, -4));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(added));
    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::AdjustVolume>(result.event()));
    TEST_ASSERT_EQUAL_INT8(-4, std::get<SwingMetro::AdjustVolume>(result.event()).delta);
}

void test_main_context_maps_all_step_clicks() {
    const SwingMetro::MainDisplayContext context;

    for (std::uint8_t step = 0; step < STEPS_COUNT; ++step) {
        const auto result = context.handle(
            makeButtonInput(SwingMetro::inputIdForStep(step), ContextInput::ButtonPhase::Clicked));

        TEST_ASSERT_TRUE(result.hasEvent());
        TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::ToggleStep>(result.event()));
        TEST_ASSERT_EQUAL_UINT8(step, std::get<SwingMetro::ToggleStep>(result.event()).step);
    }
}

void test_main_context_maps_all_step_long_presses() {
    const SwingMetro::MainDisplayContext context;

    for (std::uint8_t step = 0; step < STEPS_COUNT; ++step) {
        const auto result = context.handle(makeButtonInput(SwingMetro::inputIdForStep(step),
                                                           ContextInput::ButtonPhase::LongPressed));

        TEST_ASSERT_TRUE(result.hasEvent());
        TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::OpenStepSettings>(result.event()));
        TEST_ASSERT_EQUAL_UINT8(step, std::get<SwingMetro::OpenStepSettings>(result.event()).step);
    }
}

void test_main_context_passes_step_edges_and_unknown_button_sources() {
    const SwingMetro::MainDisplayContext context;
    const auto step = SwingMetro::InputId::Step5;
    const auto unknown = static_cast<SwingMetro::InputId>(0xFF);

    TEST_ASSERT_FALSE(
        context.handle(makeButtonInput(step, ContextInput::ButtonPhase::Pressed)).hasEvent());
    TEST_ASSERT_FALSE(
        context.handle(makeButtonInput(step, ContextInput::ButtonPhase::Released)).hasEvent());
    TEST_ASSERT_FALSE(context
                          .handle(makeButtonInput(SwingMetro::InputId::TempoEncoder,
                                                  ContextInput::ButtonPhase::Clicked))
                          .hasEvent());
    TEST_ASSERT_FALSE(
        context.handle(makeButtonInput(unknown, ContextInput::ButtonPhase::Clicked)).hasEvent());
    TEST_ASSERT_FALSE(context.handle(makeEncoderInput(step, 1)).hasEvent());
}

} // namespace

void test_main_display_context_main() {
    RUN_TEST(test_main_context_maps_tempo_encoder_with_signed_delta);
    RUN_TEST(test_main_context_maps_swing_encoder);
    RUN_TEST(test_main_context_maps_volume_encoder);
    RUN_TEST(test_main_context_passes_non_encoder_payload);
    RUN_TEST(test_main_context_passes_unknown_source);
    RUN_TEST(test_main_context_works_through_router);
    RUN_TEST(test_main_context_maps_all_step_clicks);
    RUN_TEST(test_main_context_maps_all_step_long_presses);
    RUN_TEST(test_main_context_passes_step_edges_and_unknown_button_sources);
}
