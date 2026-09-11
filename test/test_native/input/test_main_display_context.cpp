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

} // namespace

void test_main_display_context_main() {
    RUN_TEST(test_main_context_maps_tempo_encoder_with_signed_delta);
    RUN_TEST(test_main_context_maps_swing_encoder);
    RUN_TEST(test_main_context_maps_volume_encoder);
    RUN_TEST(test_main_context_passes_non_encoder_payload);
    RUN_TEST(test_main_context_passes_unknown_source);
    RUN_TEST(test_main_context_works_through_router);
}
