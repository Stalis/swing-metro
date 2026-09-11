#include "test_dispatch_result.h"

#include <context_input.h>
#include <cstdint>
#include <type_traits>
#include <unity.h>
#include <utility>
#include <variant>

namespace {

enum class TestInputId : std::uint8_t {
    Encoder,
    BlockedEncoder,
    Unknown,
};

struct AdjustValue {
    std::int8_t delta;
};

using TestAppEvent = std::variant<AdjustValue>;
using TestInputEvent = ContextInput::InputEvent<TestInputId>;
using TestDispatchResult = ContextInput::DispatchResult<TestAppEvent>;

class TestContext {
  public:
    TestDispatchResult handle(const TestInputEvent& event) {
        ++callCount_;

        if (event.source == TestInputId::BlockedEncoder) {
            return TestDispatchResult::consume();
        }

        if (event.source != TestInputId::Encoder) {
            return TestDispatchResult::pass();
        }

        const auto* encoderInput = std::get_if<ContextInput::EncoderInput>(&event.payload);
        if (encoderInput == nullptr) {
            return TestDispatchResult::pass();
        }

        return TestDispatchResult::emit(TestAppEvent{AdjustValue{encoderInput->delta}});
    }

    [[nodiscard]] std::uint8_t callCount() const { return callCount_; }

  private:
    std::uint8_t callCount_ = 0;
};

struct NonDefaultConstructibleEvent {
    explicit NonDefaultConstructibleEvent(std::int8_t value) : value{value} {}

    std::int8_t value;
};

struct MoveOnlyEvent {
    explicit MoveOnlyEvent(std::int8_t value) : value{value} {}

    MoveOnlyEvent(const MoveOnlyEvent&) = delete;
    MoveOnlyEvent& operator=(const MoveOnlyEvent&) = delete;
    MoveOnlyEvent(MoveOnlyEvent&&) = default;
    MoveOnlyEvent& operator=(MoveOnlyEvent&&) = default;

    std::int8_t value;
};

static_assert(!std::is_default_constructible_v<ContextInput::DispatchResult<TestAppEvent>>);
static_assert(!std::is_default_constructible_v<NonDefaultConstructibleEvent>);
static_assert(!std::is_copy_constructible_v<MoveOnlyEvent>);

void test_pass_is_unhandled_without_event() {
    const auto result = TestDispatchResult::pass();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Unhandled),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_FALSE(result.hasEvent());
}

void test_consume_is_consumed_without_event() {
    const auto result = TestDispatchResult::consume();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Consumed),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_FALSE(result.hasEvent());
}

void test_emit_preserves_exact_event() {
    constexpr std::int8_t expectedDelta = -4;
    const auto result = TestDispatchResult::emit(TestAppEvent{AdjustValue{expectedDelta}});

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Emitted),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_TRUE(result.hasEvent());
    TEST_ASSERT_TRUE(std::holds_alternative<AdjustValue>(result.event()));
    TEST_ASSERT_EQUAL_INT8(expectedDelta, std::get<AdjustValue>(result.event()).delta);
}

void test_mutable_event_access_updates_emitted_event() {
    auto result = TestDispatchResult::emit(TestAppEvent{AdjustValue{1}});

    std::get<AdjustValue>(result.event()).delta = 2;

    TEST_ASSERT_EQUAL_INT8(2, std::get<AdjustValue>(result.event()).delta);
}

void test_context_emits_signed_encoder_deltas() {
    TestContext context;
    constexpr std::int8_t deltas[] = {1, -1, 5};

    for (const auto delta : deltas) {
        const auto result =
            context.handle(TestInputEvent{TestInputId::Encoder, ContextInput::EncoderInput{delta}});

        TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Emitted),
                                static_cast<std::uint8_t>(result.status()));
        TEST_ASSERT_EQUAL_INT8(delta, std::get<AdjustValue>(result.event()).delta);
    }
}

void test_context_passes_unknown_source_and_wrong_payload() {
    TestContext context;
    const auto unknownResult =
        context.handle(TestInputEvent{TestInputId::Unknown, ContextInput::EncoderInput{1}});
    const auto wrongPayloadResult = context.handle(TestInputEvent{
        TestInputId::Encoder,
        ContextInput::ButtonInput{ContextInput::ButtonPhase::Pressed},
    });

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Unhandled),
                            static_cast<std::uint8_t>(unknownResult.status()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Unhandled),
                            static_cast<std::uint8_t>(wrongPayloadResult.status()));
    TEST_ASSERT_FALSE(unknownResult.hasEvent());
    TEST_ASSERT_FALSE(wrongPayloadResult.hasEvent());
}

void test_context_consumes_blocked_source() {
    TestContext context;
    const auto result =
        context.handle(TestInputEvent{TestInputId::BlockedEncoder, ContextInput::EncoderInput{1}});

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Consumed),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_FALSE(result.hasEvent());
}

void test_context_keeps_state_in_original_object() {
    TestContext context;

    context.handle(TestInputEvent{TestInputId::Unknown, ContextInput::EncoderInput{1}});
    context.handle(TestInputEvent{TestInputId::Encoder, ContextInput::EncoderInput{-1}});

    TEST_ASSERT_EQUAL_UINT8(2, context.callCount());
}

void test_result_supports_non_default_constructible_event() {
    constexpr std::int8_t expectedValue = 7;
    using Result = ContextInput::DispatchResult<NonDefaultConstructibleEvent>;

    const auto passResult = Result::pass();
    const auto consumeResult = Result::consume();
    const auto emittedResult = Result::emit(NonDefaultConstructibleEvent{expectedValue});

    TEST_ASSERT_FALSE(passResult.hasEvent());
    TEST_ASSERT_FALSE(consumeResult.hasEvent());
    TEST_ASSERT_EQUAL_INT8(expectedValue, emittedResult.event().value);
}

void test_result_supports_move_only_event() {
    constexpr std::int8_t expectedValue = 9;
    const auto result =
        ContextInput::DispatchResult<MoveOnlyEvent>::emit(MoveOnlyEvent{expectedValue});

    TEST_ASSERT_EQUAL_INT8(expectedValue, result.event().value);
}

} // namespace

void test_dispatch_result_main() {
    RUN_TEST(test_pass_is_unhandled_without_event);
    RUN_TEST(test_consume_is_consumed_without_event);
    RUN_TEST(test_emit_preserves_exact_event);
    RUN_TEST(test_mutable_event_access_updates_emitted_event);
    RUN_TEST(test_context_emits_signed_encoder_deltas);
    RUN_TEST(test_context_passes_unknown_source_and_wrong_payload);
    RUN_TEST(test_context_consumes_blocked_source);
    RUN_TEST(test_context_keeps_state_in_original_object);
    RUN_TEST(test_result_supports_non_default_constructible_event);
    RUN_TEST(test_result_supports_move_only_event);
}
