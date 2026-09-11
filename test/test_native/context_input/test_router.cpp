#include "test_router.h"

#include <context_input.h>
#include <cstddef>
#include <cstdint>
#include <unity.h>
#include <variant>

namespace {

enum class TestInputId : std::uint8_t {
    Encoder,
};

struct RoutedEvent {
    std::uint8_t contextId;
    std::int8_t delta;
};

using TestInputEvent = ContextInput::InputEvent<TestInputId>;
using TestOutputEvent = std::variant<RoutedEvent>;
using TestResult = ContextInput::DispatchResult<TestOutputEvent>;
using TestRouter = ContextInput::Router<TestInputEvent, TestOutputEvent, 3>;

enum class ContextBehavior : std::uint8_t {
    Pass,
    Consume,
    Emit,
};

class ConfigurableContext {
  public:
    ConfigurableContext(std::uint8_t contextId, ContextBehavior behavior)
        : contextId_{contextId}, behavior_{behavior} {}

    auto handle(const TestInputEvent& event) -> TestResult {
        ++callCount_;

        if (behavior_ == ContextBehavior::Pass) {
            return TestResult::pass();
        }

        if (behavior_ == ContextBehavior::Consume) {
            return TestResult::consume();
        }

        const auto* encoderInput = std::get_if<ContextInput::EncoderInput>(&event.payload);
        if (encoderInput == nullptr) {
            return TestResult::pass();
        }

        return TestResult::emit(TestOutputEvent{RoutedEvent{contextId_, encoderInput->delta}});
    }

    auto setBehavior(ContextBehavior behavior) -> void { behavior_ = behavior; }

    [[nodiscard]] auto callCount() const -> std::size_t { return callCount_; }

  private:
    std::uint8_t contextId_;
    ContextBehavior behavior_;
    std::size_t callCount_ = 0;
};

class AlternateContext {
  public:
    auto handle(const TestInputEvent& event) const -> TestResult {
        const auto* encoderInput = std::get_if<ContextInput::EncoderInput>(&event.payload);
        if (encoderInput == nullptr) {
            return TestResult::pass();
        }

        return TestResult::emit(TestOutputEvent{RoutedEvent{contextId, encoderInput->delta}});
    }

  private:
    static constexpr std::uint8_t contextId = 42;
};

class WrongResultContext {
  public:
    auto handle(const TestInputEvent&) -> bool { return true; }
};

class MissingHandleContext {};

static_assert(
    ContextInput::Detail::isCompatibleContextV<ConfigurableContext, TestInputEvent, TestResult>);
static_assert(
    ContextInput::Detail::isCompatibleContextV<AlternateContext, TestInputEvent, TestResult>);
static_assert(
    !ContextInput::Detail::isCompatibleContextV<WrongResultContext, TestInputEvent, TestResult>);
static_assert(
    !ContextInput::Detail::isCompatibleContextV<MissingHandleContext, TestInputEvent, TestResult>);

struct MoveOnlyEvent {
    explicit MoveOnlyEvent(std::int8_t delta) : delta{delta} {}

    MoveOnlyEvent(const MoveOnlyEvent&) = delete;
    auto operator=(const MoveOnlyEvent&) -> MoveOnlyEvent& = delete;
    MoveOnlyEvent(MoveOnlyEvent&&) = default;
    auto operator=(MoveOnlyEvent&&) -> MoveOnlyEvent& = default;

    std::int8_t delta;
};

class MoveOnlyContext {
  public:
    auto handle(const TestInputEvent& event) const -> ContextInput::DispatchResult<MoveOnlyEvent> {
        const auto& encoderInput = std::get<ContextInput::EncoderInput>(event.payload);
        return ContextInput::DispatchResult<MoveOnlyEvent>::emit(MoveOnlyEvent{encoderInput.delta});
    }
};

constexpr auto makeInput(std::int8_t delta = 1) -> TestInputEvent {
    return TestInputEvent{TestInputId::Encoder, ContextInput::EncoderInput{delta}};
}

auto emittedEvent(const TestResult& result) -> const RoutedEvent& {
    return std::get<RoutedEvent>(result.event());
}

template <typename TRouter, typename TContext>
void addContext(TRouter& router, TContext& context) {
    const auto result = router.addContext(context);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(result));
}

template <typename TRouter, typename TContext>
void releaseContext(TRouter& router, TContext& context) {
    const auto result = router.releaseContext(context);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::ReleaseContextResult::Released),
                            static_cast<std::uint8_t>(result));
}

void test_empty_router_is_unhandled_and_reports_capacity() {
    TestRouter router;

    const auto result = router.dispatch(makeInput());

    static_assert(TestRouter::capacity() == 3);
    TEST_ASSERT_EQUAL_UINT32(0, router.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Unhandled),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_FALSE(result.hasEvent());
}

void test_add_tracks_identity_size_and_duplicate() {
    TestRouter router;
    ConfigurableContext context{1, ContextBehavior::Pass};

    const auto added = router.addContext(context);
    const auto duplicate = router.addContext(context);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(added));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(ContextInput::AddContextResult::AlreadyPresent),
        static_cast<std::uint8_t>(duplicate));
    TEST_ASSERT_EQUAL_UINT32(1, router.size());
    TEST_ASSERT_TRUE(router.contains(context));
}

void test_stack_full_does_not_change_router() {
    ContextInput::Router<TestInputEvent, TestOutputEvent, 2> router;
    ConfigurableContext bottom{1, ContextBehavior::Emit};
    ConfigurableContext top{2, ContextBehavior::Emit};
    ConfigurableContext rejected{3, ContextBehavior::Emit};

    addContext(router, bottom);
    addContext(router, top);
    const auto result = router.addContext(rejected);
    const auto dispatchResult = router.dispatch(makeInput());

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::StackFull),
                            static_cast<std::uint8_t>(result));
    TEST_ASSERT_EQUAL_UINT32(2, router.size());
    TEST_ASSERT_FALSE(router.contains(rejected));
    TEST_ASSERT_EQUAL_UINT8(2, emittedEvent(dispatchResult).contextId);
}

void test_duplicate_has_priority_over_stack_full() {
    ContextInput::Router<TestInputEvent, TestOutputEvent, 1> router;
    ConfigurableContext context{1, ContextBehavior::Pass};

    addContext(router, context);
    const auto result = router.addContext(context);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(ContextInput::AddContextResult::AlreadyPresent),
        static_cast<std::uint8_t>(result));
}

void test_single_context_emits_exact_event() {
    TestRouter router;
    ConfigurableContext context{7, ContextBehavior::Emit};

    addContext(router, context);
    const auto result = router.dispatch(makeInput(-5));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Emitted),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_EQUAL_UINT8(7, emittedEvent(result).contextId);
    TEST_ASSERT_EQUAL_INT8(-5, emittedEvent(result).delta);
    TEST_ASSERT_EQUAL_UINT32(1, context.callCount());
}

void test_top_emitted_context_stops_dispatch() {
    TestRouter router;
    ConfigurableContext bottom{1, ContextBehavior::Emit};
    ConfigurableContext top{2, ContextBehavior::Emit};

    addContext(router, bottom);
    addContext(router, top);
    const auto result = router.dispatch(makeInput());

    TEST_ASSERT_EQUAL_UINT8(2, emittedEvent(result).contextId);
    TEST_ASSERT_EQUAL_UINT32(0, bottom.callCount());
    TEST_ASSERT_EQUAL_UINT32(1, top.callCount());
}

void test_unhandled_event_falls_through_to_lower_context() {
    TestRouter router;
    ConfigurableContext bottom{1, ContextBehavior::Emit};
    ConfigurableContext middle{2, ContextBehavior::Pass};
    ConfigurableContext top{3, ContextBehavior::Pass};

    addContext(router, bottom);
    addContext(router, middle);
    addContext(router, top);
    const auto result = router.dispatch(makeInput());

    TEST_ASSERT_EQUAL_UINT8(1, emittedEvent(result).contextId);
    TEST_ASSERT_EQUAL_UINT32(1, bottom.callCount());
    TEST_ASSERT_EQUAL_UINT32(1, middle.callCount());
    TEST_ASSERT_EQUAL_UINT32(1, top.callCount());
}

void test_consumed_event_stops_dispatch_without_output() {
    TestRouter router;
    ConfigurableContext bottom{1, ContextBehavior::Emit};
    ConfigurableContext top{2, ContextBehavior::Consume};

    addContext(router, bottom);
    addContext(router, top);
    const auto result = router.dispatch(makeInput());

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Consumed),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_FALSE(result.hasEvent());
    TEST_ASSERT_EQUAL_UINT32(0, bottom.callCount());
    TEST_ASSERT_EQUAL_UINT32(1, top.callCount());
}

void test_all_unhandled_contexts_return_unhandled() {
    TestRouter router;
    ConfigurableContext bottom{1, ContextBehavior::Pass};
    ConfigurableContext top{2, ContextBehavior::Pass};

    addContext(router, bottom);
    addContext(router, top);
    const auto result = router.dispatch(makeInput());

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Unhandled),
                            static_cast<std::uint8_t>(result.status()));
    TEST_ASSERT_EQUAL_UINT32(1, bottom.callCount());
    TEST_ASSERT_EQUAL_UINT32(1, top.callCount());
}

void test_router_accepts_unrelated_context_types() {
    TestRouter router;
    ConfigurableContext bottom{1, ContextBehavior::Pass};
    AlternateContext top;

    addContext(router, bottom);
    addContext(router, top);
    const auto result = router.dispatch(makeInput(-2));

    TEST_ASSERT_EQUAL_UINT8(42, emittedEvent(result).contextId);
    TEST_ASSERT_EQUAL_INT8(-2, emittedEvent(result).delta);
}

void test_release_top_reveals_lower_context() {
    TestRouter router;
    ConfigurableContext bottom{1, ContextBehavior::Emit};
    ConfigurableContext top{2, ContextBehavior::Emit};

    addContext(router, bottom);
    addContext(router, top);
    const auto released = router.releaseContext(top);
    const auto result = router.dispatch(makeInput());

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::ReleaseContextResult::Released),
                            static_cast<std::uint8_t>(released));
    TEST_ASSERT_EQUAL_UINT8(1, emittedEvent(result).contextId);
    TEST_ASSERT_FALSE(router.contains(top));
    TEST_ASSERT_EQUAL_UINT32(1, router.size());
}

void test_release_bottom_preserves_top_context() {
    TestRouter router;
    ConfigurableContext bottom{1, ContextBehavior::Emit};
    ConfigurableContext top{2, ContextBehavior::Emit};

    addContext(router, bottom);
    addContext(router, top);
    releaseContext(router, bottom);
    const auto result = router.dispatch(makeInput());

    TEST_ASSERT_EQUAL_UINT8(2, emittedEvent(result).contextId);
    TEST_ASSERT_FALSE(router.contains(bottom));
    TEST_ASSERT_TRUE(router.contains(top));
}

void test_release_middle_preserves_relative_order() {
    TestRouter router;
    ConfigurableContext bottom{1, ContextBehavior::Emit};
    ConfigurableContext middle{2, ContextBehavior::Emit};
    ConfigurableContext top{3, ContextBehavior::Pass};

    addContext(router, bottom);
    addContext(router, middle);
    addContext(router, top);
    releaseContext(router, middle);
    const auto result = router.dispatch(makeInput());

    TEST_ASSERT_EQUAL_UINT8(1, emittedEvent(result).contextId);
    TEST_ASSERT_EQUAL_UINT32(1, top.callCount());
    TEST_ASSERT_EQUAL_UINT32(0, middle.callCount());
    TEST_ASSERT_EQUAL_UINT32(1, bottom.callCount());
    TEST_ASSERT_EQUAL_UINT32(2, router.size());
}

void test_release_unknown_context_does_not_change_router() {
    TestRouter router;
    ConfigurableContext registered{1, ContextBehavior::Emit};
    ConfigurableContext unknown{2, ContextBehavior::Emit};

    addContext(router, registered);
    const auto result = router.releaseContext(unknown);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::ReleaseContextResult::NotFound),
                            static_cast<std::uint8_t>(result));
    TEST_ASSERT_EQUAL_UINT32(1, router.size());
    TEST_ASSERT_TRUE(router.contains(registered));
}

void test_released_context_keeps_state_and_can_be_added_again() {
    TestRouter router;
    ConfigurableContext context{1, ContextBehavior::Pass};

    addContext(router, context);
    const auto firstResult = router.dispatch(makeInput());
    releaseContext(router, context);
    context.setBehavior(ContextBehavior::Emit);
    addContext(router, context);
    const auto result = router.dispatch(makeInput());

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Unhandled),
                            static_cast<std::uint8_t>(firstResult.status()));
    TEST_ASSERT_EQUAL_UINT8(1, emittedEvent(result).contextId);
    TEST_ASSERT_EQUAL_UINT32(2, context.callCount());
}

void test_router_supports_move_only_output_event() {
    ContextInput::Router<TestInputEvent, MoveOnlyEvent, 1> router;
    MoveOnlyContext context;

    addContext(router, context);
    const auto result = router.dispatch(makeInput(-7));

    TEST_ASSERT_TRUE(result.hasEvent());
    TEST_ASSERT_EQUAL_INT8(-7, result.event().delta);
}

} // namespace

void test_router_main() {
    RUN_TEST(test_empty_router_is_unhandled_and_reports_capacity);
    RUN_TEST(test_add_tracks_identity_size_and_duplicate);
    RUN_TEST(test_stack_full_does_not_change_router);
    RUN_TEST(test_duplicate_has_priority_over_stack_full);
    RUN_TEST(test_single_context_emits_exact_event);
    RUN_TEST(test_top_emitted_context_stops_dispatch);
    RUN_TEST(test_unhandled_event_falls_through_to_lower_context);
    RUN_TEST(test_consumed_event_stops_dispatch_without_output);
    RUN_TEST(test_all_unhandled_contexts_return_unhandled);
    RUN_TEST(test_router_accepts_unrelated_context_types);
    RUN_TEST(test_release_top_reveals_lower_context);
    RUN_TEST(test_release_bottom_preserves_top_context);
    RUN_TEST(test_release_middle_preserves_relative_order);
    RUN_TEST(test_release_unknown_context_does_not_change_router);
    RUN_TEST(test_released_context_keeps_state_and_can_be_added_again);
    RUN_TEST(test_router_supports_move_only_output_event);
}
