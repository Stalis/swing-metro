#include "test_trigger_input_adapter.h"

#include <adapters/trigger_input.h>
#include <context_input.h>
#include <cstdint>
#include <optional>
#include <unity.h>
#include <variant>

namespace {

enum class TestInputId : std::uint8_t {
    Shift,
    Mode,
};

struct ActivateShift {};
struct DeactivateShift {};
struct BaseAction {};
struct ShiftAction {};
struct OverlayAction {};

using TestInputEvent = ContextInput::InputEvent<TestInputId>;
using TestOutputEvent =
    std::variant<ActivateShift, DeactivateShift, BaseAction, ShiftAction, OverlayAction>;
using TestResult = ContextInput::DispatchResult<TestOutputEvent>;
using TestRouter = ContextInput::Router<TestInputEvent, TestOutputEvent, 3>;
using TestAdapter = ContextInput::TriggerInputAdapter<TestInputId>;

class BaseContext {
  public:
    [[nodiscard]] auto handle(const TestInputEvent& event) const -> TestResult {
        const auto* trigger = std::get_if<ContextInput::TriggerInput>(&event.payload);
        if (trigger == nullptr) {
            return TestResult::pass();
        }

        if (event.source == TestInputId::Shift) {
            if (trigger->active) {
                return TestResult::emit(TestOutputEvent{ActivateShift{}});
            }
            return TestResult::emit(TestOutputEvent{DeactivateShift{}});
        }

        if (event.source == TestInputId::Mode && trigger->active) {
            return TestResult::emit(TestOutputEvent{BaseAction{}});
        }

        return TestResult::pass();
    }
};

class ShiftContext {
  public:
    [[nodiscard]] auto handle(const TestInputEvent& event) -> TestResult {
        ++callCount;

        if (event.source != TestInputId::Mode) {
            return TestResult::pass();
        }

        const auto* trigger = std::get_if<ContextInput::TriggerInput>(&event.payload);
        if (trigger != nullptr && trigger->active) {
            return TestResult::emit(TestOutputEvent{ShiftAction{}});
        }

        return TestResult::pass();
    }

    std::uint32_t callCount = 0;
};

class OverlayContext {
  public:
    [[nodiscard]] auto handle(const TestInputEvent& event) const -> TestResult {
        if (event.source != TestInputId::Mode) {
            return TestResult::pass();
        }

        const auto* trigger = std::get_if<ContextInput::TriggerInput>(&event.payload);
        if (trigger != nullptr && trigger->active) {
            return TestResult::emit(TestOutputEvent{OverlayAction{}});
        }

        return TestResult::pass();
    }
};

auto triggerValue(const std::optional<TestInputEvent>& event) -> bool {
    TEST_ASSERT_TRUE(event.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<ContextInput::TriggerInput>(event->payload));
    return std::get<ContextInput::TriggerInput>(event->payload).active;
}

void test_false_to_true_emits_active_with_source() {
    TestAdapter adapter{TestInputId::Shift};

    const auto event = adapter.set(true);

    TEST_ASSERT_TRUE(triggerValue(event));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::Shift),
                            static_cast<std::uint8_t>(event->source));
    TEST_ASSERT_TRUE(adapter.isActive());
}

void test_true_to_false_emits_inactive_with_source() {
    TestAdapter adapter{TestInputId::Mode, true};

    const auto event = adapter.set(false);

    TEST_ASSERT_FALSE(triggerValue(event));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::Mode),
                            static_cast<std::uint8_t>(event->source));
    TEST_ASSERT_FALSE(adapter.isActive());
}

void test_unchanged_state_emits_nothing() {
    TestAdapter adapter{TestInputId::Shift};

    const auto initialRepeat = adapter.set(false);
    const auto activated = adapter.set(true);
    const auto activeRepeat = adapter.set(true);
    const auto deactivated = adapter.set(false);
    const auto inactiveRepeat = adapter.set(false);

    TEST_ASSERT_FALSE(initialRepeat.has_value());
    TEST_ASSERT_TRUE(triggerValue(activated));
    TEST_ASSERT_FALSE(activeRepeat.has_value());
    TEST_ASSERT_FALSE(triggerValue(deactivated));
    TEST_ASSERT_FALSE(inactiveRepeat.has_value());
}

void test_configurable_initial_state() {
    TestAdapter adapter{TestInputId::Shift, true};

    TEST_ASSERT_TRUE(adapter.isActive());
    TEST_ASSERT_FALSE(adapter.set(true).has_value());
    TEST_ASSERT_FALSE(triggerValue(adapter.set(false)));
    TEST_ASSERT_FALSE(adapter.isActive());
}

void test_two_triggers_keep_independent_state_and_sources() {
    TestAdapter shift{TestInputId::Shift};
    TestAdapter mode{TestInputId::Mode, true};

    const auto shiftEvent = shift.set(true);
    const auto modeEvent = mode.set(false);

    TEST_ASSERT_TRUE(shift.isActive());
    TEST_ASSERT_FALSE(mode.isActive());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::Shift),
                            static_cast<std::uint8_t>(shiftEvent->source));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TestInputId::Mode),
                            static_cast<std::uint8_t>(modeEvent->source));
    TEST_ASSERT_TRUE(triggerValue(shiftEvent));
    TEST_ASSERT_FALSE(triggerValue(modeEvent));
}

void test_shift_context_lifecycle_after_dispatch() {
    TestRouter router;
    BaseContext base;
    ShiftContext shifted;
    TestAdapter shift{TestInputId::Shift};
    const TestInputEvent modeEvent{TestInputId::Mode, ContextInput::TriggerInput{true}};

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(router.addContext(base)));
    TEST_ASSERT_TRUE(std::holds_alternative<BaseAction>(router.dispatch(modeEvent).event()));

    const auto activateInput = shift.set(true);
    TEST_ASSERT_TRUE(activateInput.has_value());
    const auto activateResult = router.dispatch(*activateInput);
    TEST_ASSERT_TRUE(std::holds_alternative<ActivateShift>(activateResult.event()));
    TEST_ASSERT_EQUAL_UINT32(0, shifted.callCount);
    TEST_ASSERT_FALSE(router.contains(shifted));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(router.addContext(shifted)));
    TEST_ASSERT_EQUAL_UINT32(2, router.size());
    TEST_ASSERT_TRUE(std::holds_alternative<ShiftAction>(router.dispatch(modeEvent).event()));

    const auto deactivateInput = shift.set(false);
    TEST_ASSERT_TRUE(deactivateInput.has_value());
    const auto deactivateResult = router.dispatch(*deactivateInput);
    TEST_ASSERT_TRUE(std::holds_alternative<DeactivateShift>(deactivateResult.event()));
    TEST_ASSERT_TRUE(router.contains(shifted));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::ReleaseContextResult::Released),
                            static_cast<std::uint8_t>(router.releaseContext(shifted)));
    TEST_ASSERT_EQUAL_UINT32(1, router.size());
    TEST_ASSERT_TRUE(std::holds_alternative<BaseAction>(router.dispatch(modeEvent).event()));
}

void test_repeated_active_does_not_add_duplicate_shift_context() {
    TestRouter router;
    BaseContext base;
    ShiftContext shifted;
    TestAdapter shift{TestInputId::Shift};
    const auto baseAdded = router.addContext(base);
    const auto firstActivation = shift.set(true);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(baseAdded));
    TEST_ASSERT_TRUE(firstActivation.has_value());
    TEST_ASSERT_TRUE(
        std::holds_alternative<ActivateShift>(router.dispatch(*firstActivation).event()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(router.addContext(shifted)));

    const auto repeatActivation = shift.set(true);

    TEST_ASSERT_FALSE(repeatActivation.has_value());
    TEST_ASSERT_EQUAL_UINT32(2, router.size());
    TEST_ASSERT_TRUE(router.contains(shifted));
}

void test_releasing_shift_from_middle_keeps_overlay() {
    TestRouter router;
    BaseContext base;
    ShiftContext shifted;
    OverlayContext overlay;
    TestAdapter shift{TestInputId::Shift};
    const TestInputEvent modeEvent{TestInputId::Mode, ContextInput::TriggerInput{true}};
    const auto baseAdded = router.addContext(base);
    const auto activateInput = shift.set(true);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(baseAdded));
    TEST_ASSERT_TRUE(activateInput.has_value());
    TEST_ASSERT_TRUE(
        std::holds_alternative<ActivateShift>(router.dispatch(*activateInput).event()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(router.addContext(shifted)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::AddContextResult::Added),
                            static_cast<std::uint8_t>(router.addContext(overlay)));

    TEST_ASSERT_TRUE(std::holds_alternative<OverlayAction>(router.dispatch(modeEvent).event()));

    const auto deactivateInput = shift.set(false);
    TEST_ASSERT_TRUE(deactivateInput.has_value());
    TEST_ASSERT_TRUE(
        std::holds_alternative<DeactivateShift>(router.dispatch(*deactivateInput).event()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::ReleaseContextResult::Released),
                            static_cast<std::uint8_t>(router.releaseContext(shifted)));
    TEST_ASSERT_EQUAL_UINT32(2, router.size());
    TEST_ASSERT_FALSE(router.contains(shifted));
    TEST_ASSERT_TRUE(router.contains(overlay));
    TEST_ASSERT_TRUE(std::holds_alternative<OverlayAction>(router.dispatch(modeEvent).event()));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::ReleaseContextResult::Released),
                            static_cast<std::uint8_t>(router.releaseContext(overlay)));
    TEST_ASSERT_TRUE(std::holds_alternative<BaseAction>(router.dispatch(modeEvent).event()));
}

} // namespace

void test_trigger_input_adapter_main() {
    RUN_TEST(test_false_to_true_emits_active_with_source);
    RUN_TEST(test_true_to_false_emits_inactive_with_source);
    RUN_TEST(test_unchanged_state_emits_nothing);
    RUN_TEST(test_configurable_initial_state);
    RUN_TEST(test_two_triggers_keep_independent_state_and_sources);
    RUN_TEST(test_shift_context_lifecycle_after_dispatch);
    RUN_TEST(test_repeated_active_does_not_add_duplicate_shift_context);
    RUN_TEST(test_releasing_shift_from_middle_keeps_overlay);
}
