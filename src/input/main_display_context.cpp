#include "main_display_context.h"

namespace SwingMetro {

// Context handlers intentionally use the same instance-method contract, even when stateless.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto MainDisplayContext::handle(const InputEvent& event) const
    -> ContextInput::DispatchResult<AppEvent> {
    using Result = ContextInput::DispatchResult<AppEvent>;

    if (const auto* encoderInput = std::get_if<ContextInput::EncoderInput>(&event.payload)) {
        switch (event.source) {
        case InputId::TempoEncoder:
            return Result::emit(AppEvent{AdjustTempo{encoderInput->delta}});
        case InputId::SwingEncoder:
            return Result::emit(AppEvent{AdjustSwing{encoderInput->delta}});
        case InputId::VolumeEncoder:
            return Result::emit(AppEvent{AdjustVolume{encoderInput->delta}});
        default:
            return Result::pass();
        }
    }

    const auto* buttonInput = std::get_if<ContextInput::ButtonInput>(&event.payload);
    const auto step = stepIndexFromInputId(event.source);
    if (buttonInput == nullptr || !step.has_value()) {
        return Result::pass();
    }

    switch (buttonInput->phase) {
    case ContextInput::ButtonPhase::Clicked:
        return Result::emit(AppEvent{ToggleStep{*step}});
    case ContextInput::ButtonPhase::LongPressed:
        return Result::emit(AppEvent{OpenStepSettings{*step}});
    case ContextInput::ButtonPhase::Pressed:
    case ContextInput::ButtonPhase::Released:
        return Result::pass();
    }

    return Result::pass();
}

} // namespace SwingMetro
