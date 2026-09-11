#include "test_encoder_input_adapter.h"

#include <adapters/encoder_input.h>
#include <context_input.h>
#include <cstdint>
#include <optional>
#include <unity.h>
#include <variant>

namespace {

enum class TestInputId : std::uint8_t {
    TempoEncoder,
    SwingEncoder,
};

struct AdjustTempo {
    std::int8_t delta;
};

using TestInputEvent = ContextInput::InputEvent<TestInputId>;
using TestOutputEvent = std::variant<AdjustTempo>;
using TestResult = ContextInput::DispatchResult<TestOutputEvent>;
using TestRouter = ContextInput::Router<TestInputEvent, TestOutputEvent, 1>;
using TestAdapter = ContextInput::EncoderInputAdapter<TestInputId>;

class TempoContext {
  public:
    auto handle(const TestInputEvent& event) const -> TestResult {
        if (event.source != TestInputId::TempoEncoder) {
            return TestResult::pass();
        }

        const auto* encoderInput = std::get_if<ContextInput::EncoderInput>(&event.payload);
        if (encoderInput == nullptr) {
            return TestResult::pass();
        }

        return TestResult::emit(TestOutputEvent{AdjustTempo{encoderInput->delta}});
    }
};

auto encoderInput(const std::optional<TestInputEvent>& event) -> const ContextInput::EncoderInput& {
    TEST_ASSERT_TRUE(event.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<ContextInput::EncoderInput>(event->payload));
    return std::get<ContextInput::EncoderInput>(event->payload);
}

void test_right_creates_positive_encoder_input() {
    const TestAdapter adapter{TestInputId::TempoEncoder};

    const auto event = adapter.translate(EncoderDirection::Right);

    TEST_ASSERT_TRUE(event.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::TempoEncoder),
                            static_cast<std::uint8_t>(event->source));
    TEST_ASSERT_EQUAL_INT8(1, encoderInput(event).delta);
}

void test_left_creates_negative_encoder_input() {
    const TestAdapter adapter{TestInputId::TempoEncoder};

    const auto event = adapter.translate(EncoderDirection::Left);

    TEST_ASSERT_TRUE(event.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::TempoEncoder),
                            static_cast<std::uint8_t>(event->source));
    TEST_ASSERT_EQUAL_INT8(-1, encoderInput(event).delta);
}

void test_canonical_directions_match_aliases() {
    const TestAdapter adapter{TestInputId::TempoEncoder};

    const auto clockwise = adapter.translate(EncoderDirection::Clockwise);
    const auto right = adapter.translate(EncoderDirection::Right);
    const auto counterClockwise = adapter.translate(EncoderDirection::CounterClockwise);
    const auto left = adapter.translate(EncoderDirection::Left);

    TEST_ASSERT_EQUAL_INT8(encoderInput(right).delta, encoderInput(clockwise).delta);
    TEST_ASSERT_EQUAL_INT8(encoderInput(left).delta, encoderInput(counterClockwise).delta);
}

void test_undefined_direction_creates_no_event() {
    const TestAdapter adapter{TestInputId::TempoEncoder};

    const auto event = adapter.translate(EncoderDirection::Undefined);

    TEST_ASSERT_FALSE(event.has_value());
}

void test_unknown_direction_creates_no_event() {
    const TestAdapter adapter{TestInputId::TempoEncoder};

    const auto event = adapter.translate(static_cast<EncoderDirection>(0x7F));

    TEST_ASSERT_FALSE(event.has_value());
}

void test_adapters_keep_distinct_source_ids() {
    const TestAdapter tempoAdapter{TestInputId::TempoEncoder};
    const TestAdapter swingAdapter{TestInputId::SwingEncoder};

    const auto tempoEvent = tempoAdapter.translate(EncoderDirection::Right);
    const auto swingEvent = swingAdapter.translate(EncoderDirection::Right);

    TEST_ASSERT_TRUE(tempoEvent.has_value());
    TEST_ASSERT_TRUE(swingEvent.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::TempoEncoder),
                            static_cast<std::uint8_t>(tempoEvent->source));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::SwingEncoder),
                            static_cast<std::uint8_t>(swingEvent->source));
}

void test_adapter_can_translate_repeatedly_without_state() {
    const TestAdapter adapter{TestInputId::TempoEncoder};

    const auto first = adapter.translate(EncoderDirection::Right);
    const auto second = adapter.translate(EncoderDirection::Left);
    const auto third = adapter.translate(EncoderDirection::Right);

    TEST_ASSERT_EQUAL_INT8(1, encoderInput(first).delta);
    TEST_ASSERT_EQUAL_INT8(-1, encoderInput(second).delta);
    TEST_ASSERT_EQUAL_INT8(1, encoderInput(third).delta);
}

void test_adapter_event_routes_to_semantic_event() {
    const TestAdapter adapter{TestInputId::TempoEncoder};
    TestRouter router;
    TempoContext context;
    const auto added = router.addContext(context);
    const auto input = adapter.translate(EncoderDirection::Left);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(added));
    TEST_ASSERT_TRUE(input.has_value());

    const auto result = router.dispatch(*input);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Emitted),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_EQUAL_INT8(-1, std::get<AdjustTempo>(result.event()).delta);
}

} // namespace

void test_encoder_input_adapter_main() {
    RUN_TEST(test_right_creates_positive_encoder_input);
    RUN_TEST(test_left_creates_negative_encoder_input);
    RUN_TEST(test_canonical_directions_match_aliases);
    RUN_TEST(test_undefined_direction_creates_no_event);
    RUN_TEST(test_unknown_direction_creates_no_event);
    RUN_TEST(test_adapters_keep_distinct_source_ids);
    RUN_TEST(test_adapter_can_translate_repeatedly_without_state);
    RUN_TEST(test_adapter_event_routes_to_semantic_event);
}
