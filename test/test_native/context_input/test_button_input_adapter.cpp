#include "test_button_input_adapter.h"

#include <adapters/button_input.h>
#include <array>
#include <context_input.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unity.h>
#include <variant>

namespace {

enum class TestInputId : std::uint8_t {
    TempoButton,
    SwingButton,
};

struct ObservedButton {
    ContextInput::ButtonPhase phase;
};

using TestInputEvent = ContextInput::InputEvent<TestInputId>;
using TestOutputEvent = std::variant<ObservedButton>;
using TestResult = ContextInput::DispatchResult<TestOutputEvent>;
using TestAdapter = ContextInput::ButtonInputAdapter<TestInputId>;
using TestBatch = TestAdapter::Batch;

class ButtonContext {
  public:
    auto handle(const TestInputEvent& event) const -> TestResult {
        const auto* button = std::get_if<ContextInput::ButtonInput>(&event.payload);
        if (button == nullptr) {
            return TestResult::pass();
        }

        return TestResult::emit(TestOutputEvent{ObservedButton{button->phase}});
    }
};

auto makeAdapter(TestInputId source = TestInputId::TempoButton, std::uint32_t threshold = 500)
    -> TestAdapter {
    return TestAdapter{{
        .source = source,
        .longPressThreshold = threshold,
    }};
}

auto phaseAt(const TestBatch& batch, std::size_t index) -> ContextInput::ButtonPhase {
    TEST_ASSERT_TRUE(index < batch.size());
    TEST_ASSERT_TRUE(std::holds_alternative<ContextInput::ButtonInput>(batch[index].payload));
    return std::get<ContextInput::ButtonInput>(batch[index].payload).phase;
}

void assertPhase(const TestBatch& batch, std::size_t index, ContextInput::ButtonPhase expected) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(expected),
                            static_cast<std::uint8_t>(phaseAt(batch, index)));
}

void test_press_click_release_order() {
    auto adapter = makeAdapter();

    const auto pressed = adapter.onPressed(100);
    const auto waiting = adapter.update(300);
    const auto released = adapter.onReleased(400);

    TEST_ASSERT_TRUE(adapter.isPressed() == false);
    TEST_ASSERT_EQUAL_UINT32(1, pressed.size());
    assertPhase(pressed, 0, ContextInput::ButtonPhase::Pressed);
    TEST_ASSERT_TRUE(waiting.empty());
    TEST_ASSERT_EQUAL_UINT32(2, released.size());
    assertPhase(released, 0, ContextInput::ButtonPhase::Clicked);
    assertPhase(released, 1, ContextInput::ButtonPhase::Released);
}

void test_long_press_exactly_at_threshold() {
    auto adapter = makeAdapter();
    const auto pressed = adapter.onPressed(100);

    const auto longPressed = adapter.update(600);

    TEST_ASSERT_EQUAL_UINT32(1, pressed.size());
    TEST_ASSERT_EQUAL_UINT32(1, longPressed.size());
    assertPhase(longPressed, 0, ContextInput::ButtonPhase::LongPressed);
    TEST_ASSERT_TRUE(adapter.isPressed());
}

void test_long_press_after_threshold_is_not_repeated() {
    auto adapter = makeAdapter();
    const auto pressed = adapter.onPressed(100);

    const auto longPressed = adapter.update(700);
    const auto repeated = adapter.update(900);

    TEST_ASSERT_EQUAL_UINT32(1, pressed.size());
    assertPhase(longPressed, 0, ContextInput::ButtonPhase::LongPressed);
    TEST_ASSERT_TRUE(repeated.empty());
}

void test_release_after_emitted_long_press_has_no_click() {
    auto adapter = makeAdapter();
    const auto pressed = adapter.onPressed(100);
    const auto longPressed = adapter.update(600);

    const auto released = adapter.onReleased(800);

    TEST_ASSERT_EQUAL_UINT32(1, pressed.size());
    TEST_ASSERT_EQUAL_UINT32(1, longPressed.size());
    TEST_ASSERT_EQUAL_UINT32(1, released.size());
    assertPhase(released, 0, ContextInput::ButtonPhase::Released);
}

void test_late_release_emits_long_press_then_release() {
    auto adapter = makeAdapter();
    const auto pressed = adapter.onPressed(100);

    const auto released = adapter.onReleased(700);

    TEST_ASSERT_EQUAL_UINT32(1, pressed.size());
    TEST_ASSERT_EQUAL_UINT32(2, released.size());
    assertPhase(released, 0, ContextInput::ButtonPhase::LongPressed);
    assertPhase(released, 1, ContextInput::ButtonPhase::Released);
}

void test_repeated_edges_are_ignored_and_next_press_is_fresh() {
    auto adapter = makeAdapter();
    const auto firstPress = adapter.onPressed(100);

    const auto repeatedPress = adapter.onPressed(200);
    const auto firstRelease = adapter.onReleased(300);
    const auto repeatedRelease = adapter.onReleased(400);
    const auto secondPress = adapter.onPressed(500);

    TEST_ASSERT_EQUAL_UINT32(1, firstPress.size());
    TEST_ASSERT_TRUE(repeatedPress.empty());
    TEST_ASSERT_EQUAL_UINT32(2, firstRelease.size());
    TEST_ASSERT_TRUE(repeatedRelease.empty());
    TEST_ASSERT_EQUAL_UINT32(1, secondPress.size());
    assertPhase(secondPress, 0, ContextInput::ButtonPhase::Pressed);
    TEST_ASSERT_TRUE(adapter.isPressed());
}

void test_timer_wraparound_preserves_elapsed_time() {
    auto adapter = makeAdapter(TestInputId::TempoButton, 48);
    constexpr auto pressedAt = std::numeric_limits<std::uint32_t>::max() - 15;
    const auto pressed = adapter.onPressed(pressedAt);

    const auto beforeThreshold = adapter.update(30);
    const auto atThreshold = adapter.update(32);

    TEST_ASSERT_EQUAL_UINT32(1, pressed.size());
    TEST_ASSERT_TRUE(beforeThreshold.empty());
    TEST_ASSERT_EQUAL_UINT32(1, atThreshold.size());
    assertPhase(atThreshold, 0, ContextInput::ButtonPhase::LongPressed);
}

void test_two_adapters_keep_independent_state_and_sources() {
    auto tempo = makeAdapter(TestInputId::TempoButton, 500);
    auto swing = makeAdapter(TestInputId::SwingButton, 1000);

    const auto tempoPressed = tempo.onPressed(100);
    const auto swingPressed = swing.onPressed(200);
    const auto tempoLong = tempo.update(600);
    const auto swingWaiting = swing.update(600);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::TempoButton),
                            static_cast<std::uint8_t>(tempoPressed[0].source));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::SwingButton),
                            static_cast<std::uint8_t>(swingPressed[0].source));
    assertPhase(tempoLong, 0, ContextInput::ButtonPhase::LongPressed);
    TEST_ASSERT_TRUE(swingWaiting.empty());
    TEST_ASSERT_TRUE(tempo.isPressed());
    TEST_ASSERT_TRUE(swing.isPressed());
}

void test_zero_threshold_is_immediately_due_after_press() {
    auto adapter = makeAdapter(TestInputId::TempoButton, 0);
    const auto pressed = adapter.onPressed(100);

    const auto longPressed = adapter.update(100);
    const auto released = adapter.onReleased(100);

    TEST_ASSERT_EQUAL_UINT32(1, pressed.size());
    assertPhase(longPressed, 0, ContextInput::ButtonPhase::LongPressed);
    TEST_ASSERT_EQUAL_UINT32(1, released.size());
    assertPhase(released, 0, ContextInput::ButtonPhase::Released);
}

void test_late_release_with_zero_threshold_emits_two_events() {
    auto adapter = makeAdapter(TestInputId::TempoButton, 0);
    const auto pressed = adapter.onPressed(100);

    const auto released = adapter.onReleased(100);

    TEST_ASSERT_EQUAL_UINT32(1, pressed.size());
    TEST_ASSERT_EQUAL_UINT32(2, released.size());
    assertPhase(released, 0, ContextInput::ButtonPhase::LongPressed);
    assertPhase(released, 1, ContextInput::ButtonPhase::Released);
}

void test_button_events_route_in_batch_order() {
    auto adapter = makeAdapter();
    ContextInput::Router<TestInputEvent, TestOutputEvent, 1> router;
    ButtonContext context;
    const auto added = router.addContext(context);
    const auto pressed = adapter.onPressed(100);
    const auto released = adapter.onReleased(200);
    std::array<ContextInput::ButtonPhase, 2> observed{};

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(added));
    TEST_ASSERT_EQUAL_UINT32(1, pressed.size());

    for (std::size_t index = 0; index < released.size(); ++index) {
        const auto result = router.dispatch(released[index]);
        TEST_ASSERT_TRUE(result.hasEvent());
        observed[index] = std::get<ObservedButton>(result.event()).phase;
    }

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::ButtonPhase::Clicked),
                            static_cast<std::uint8_t>(observed[0]));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::ButtonPhase::Released),
                            static_cast<std::uint8_t>(observed[1]));
}

} // namespace

void test_button_input_adapter_main() {
    RUN_TEST(test_press_click_release_order);
    RUN_TEST(test_long_press_exactly_at_threshold);
    RUN_TEST(test_long_press_after_threshold_is_not_repeated);
    RUN_TEST(test_release_after_emitted_long_press_has_no_click);
    RUN_TEST(test_late_release_emits_long_press_then_release);
    RUN_TEST(test_repeated_edges_are_ignored_and_next_press_is_fresh);
    RUN_TEST(test_timer_wraparound_preserves_elapsed_time);
    RUN_TEST(test_two_adapters_keep_independent_state_and_sources);
    RUN_TEST(test_zero_threshold_is_immediately_due_after_press);
    RUN_TEST(test_late_release_with_zero_threshold_emits_two_events);
    RUN_TEST(test_button_events_route_in_batch_order);
}
