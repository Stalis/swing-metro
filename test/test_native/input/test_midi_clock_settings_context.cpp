#include "test_midi_clock_settings_context.h"

#include "input/app_contexts.h"

#include <context_input.h>
#include <cstdint>
#include <unity.h>
#include <variant>

namespace {

template <typename TPayload>
auto input(SwingMetro::InputId source, TPayload payload) -> SwingMetro::InputEvent {
    return {source, payload};
}

void test_midi_clock_context_maps_tempo_rotation_and_confirmation() {
    const SwingMetro::MidiClockSettingsContext context;

    const auto rotation =
        context.handle(input(SwingMetro::InputId::TempoEncoder, ContextInput::EncoderInput{-1}));
    TEST_ASSERT_TRUE(rotation.hasEvent());
    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::AdjustMidiClockPreview>(rotation.event()));
    TEST_ASSERT_EQUAL_INT8(-1,
                           std::get<SwingMetro::AdjustMidiClockPreview>(rotation.event()).delta);

    const auto confirm =
        context.handle(input(SwingMetro::InputId::TempoSwitch,
                             ContextInput::ButtonInput{ContextInput::ButtonPhase::Clicked}));
    TEST_ASSERT_TRUE(confirm.hasEvent());
    TEST_ASSERT_TRUE(std::holds_alternative<SwingMetro::ConfirmMidiClockSettings>(confirm.event()));
}

void test_midi_clock_context_consumes_all_other_input() {
    const SwingMetro::MidiClockSettingsContext context;
    const auto consumedButton =
        context.handle(input(SwingMetro::InputId::TempoSwitch,
                             ContextInput::ButtonInput{ContextInput::ButtonPhase::LongPressed}));
    const auto consumedEncoder =
        context.handle(input(SwingMetro::InputId::SwingEncoder, ContextInput::EncoderInput{1}));
    const auto consumedStep = context.handle(input(
        SwingMetro::InputId::Step0, ContextInput::ButtonInput{ContextInput::ButtonPhase::Clicked}));

    TEST_ASSERT_FALSE(consumedButton.hasEvent());
    TEST_ASSERT_FALSE(consumedEncoder.hasEvent());
    TEST_ASSERT_FALSE(consumedStep.hasEvent());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Consumed),
                            static_cast<std::uint8_t>(consumedButton.status()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Consumed),
                            static_cast<std::uint8_t>(consumedEncoder.status()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ContextInput::DispatchStatus::Consumed),
                            static_cast<std::uint8_t>(consumedStep.status()));
}

} // namespace

void test_midi_clock_settings_context_main() {
    RUN_TEST(test_midi_clock_context_maps_tempo_rotation_and_confirmation);
    RUN_TEST(test_midi_clock_context_consumes_all_other_input);
}
