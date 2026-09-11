#include "test_input_event.h"

#include <context_input.h>
#include <cstdint>
#include <type_traits>
#include <unity.h>
#include <variant>

namespace {

enum class TestInputId : std::uint8_t {
    Encoder,
    Button,
    Trigger,
};

using TestInputEvent = ContextInput::InputEvent<TestInputId>;

static_assert(std::is_copy_constructible_v<TestInputEvent>);
static_assert(std::is_move_constructible_v<TestInputEvent>);

void test_encoder_input_preserves_source_and_positive_delta() {
    const TestInputEvent event{
        TestInputId::Encoder,
        ContextInput::EncoderInput{1},
    };

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::Encoder),
                            static_cast<std::uint8_t>(event.source));
    TEST_ASSERT_TRUE(std::holds_alternative<ContextInput::EncoderInput>(event.payload));
    TEST_ASSERT_EQUAL_INT8(1, std::get<ContextInput::EncoderInput>(event.payload).delta);
}

void test_encoder_input_preserves_negative_and_multi_step_deltas() {
    const TestInputEvent negativeEvent{
        TestInputId::Encoder,
        ContextInput::EncoderInput{-1},
    };
    const TestInputEvent multiStepEvent{
        TestInputId::Encoder,
        ContextInput::EncoderInput{4},
    };

    TEST_ASSERT_EQUAL_INT8(-1, std::get<ContextInput::EncoderInput>(negativeEvent.payload).delta);
    TEST_ASSERT_EQUAL_INT8(4, std::get<ContextInput::EncoderInput>(multiStepEvent.payload).delta);
}

void test_button_input_preserves_all_phases() {
    constexpr ContextInput::ButtonPhase phases[] = {
        ContextInput::ButtonPhase::Pressed,
        ContextInput::ButtonPhase::Released,
        ContextInput::ButtonPhase::Clicked,
        ContextInput::ButtonPhase::LongPressed,
    };

    for (const auto phase : phases) {
        const TestInputEvent event{
            TestInputId::Button,
            ContextInput::ButtonInput{phase},
        };

        TEST_ASSERT_TRUE(std::holds_alternative<ContextInput::ButtonInput>(event.payload));
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<std::uint8_t>(phase),
            static_cast<std::uint8_t>(std::get<ContextInput::ButtonInput>(event.payload).phase));
    }
}

void test_trigger_input_preserves_active_and_inactive_states() {
    const TestInputEvent activeEvent{
        TestInputId::Trigger,
        ContextInput::TriggerInput{true},
    };
    const TestInputEvent inactiveEvent{
        TestInputId::Trigger,
        ContextInput::TriggerInput{false},
    };

    TEST_ASSERT_TRUE(std::get<ContextInput::TriggerInput>(activeEvent.payload).active);
    TEST_ASSERT_FALSE(std::get<ContextInput::TriggerInput>(inactiveEvent.payload).active);
}

void test_input_payload_distinguishes_event_kinds() {
    const TestInputEvent encoderEvent{
        TestInputId::Encoder,
        ContextInput::EncoderInput{1},
    };
    const TestInputEvent buttonEvent{
        TestInputId::Button,
        ContextInput::ButtonInput{ContextInput::ButtonPhase::Pressed},
    };
    const TestInputEvent triggerEvent{
        TestInputId::Trigger,
        ContextInput::TriggerInput{true},
    };

    TEST_ASSERT_TRUE(std::holds_alternative<ContextInput::EncoderInput>(encoderEvent.payload));
    TEST_ASSERT_TRUE(std::holds_alternative<ContextInput::ButtonInput>(buttonEvent.payload));
    TEST_ASSERT_TRUE(std::holds_alternative<ContextInput::TriggerInput>(triggerEvent.payload));
}

void test_input_event_supports_integral_source_ids() {
    constexpr std::uint16_t sourceId = 42;
    const ContextInput::InputEvent<std::uint16_t> event{
        sourceId,
        ContextInput::TriggerInput{true},
    };

    TEST_ASSERT_EQUAL_UINT16(sourceId, event.source);
}

} // namespace

void test_input_event_main() {
    RUN_TEST(test_encoder_input_preserves_source_and_positive_delta);
    RUN_TEST(test_encoder_input_preserves_negative_and_multi_step_deltas);
    RUN_TEST(test_button_input_preserves_all_phases);
    RUN_TEST(test_trigger_input_preserves_active_and_inactive_states);
    RUN_TEST(test_input_payload_distinguishes_event_kinds);
    RUN_TEST(test_input_event_supports_integral_source_ids);
}
