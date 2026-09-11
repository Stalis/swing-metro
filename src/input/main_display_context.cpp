#include "main_display_context.h"

namespace SwingMetro {

// Context handlers intentionally use the same instance-method contract, even when stateless.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto MainDisplayContext::handle(const InputEvent& event) const
    -> ContextInput::DispatchResult<AppEvent> {
    using Result = ContextInput::DispatchResult<AppEvent>;

    const auto* encoderInput = std::get_if<ContextInput::EncoderInput>(&event.payload);
    if (encoderInput == nullptr) {
        return Result::pass();
    }

    switch (event.source) {
    case InputId::TempoEncoder:
        return Result::emit(AppEvent{AdjustTempo{encoderInput->delta}});
    case InputId::SwingEncoder:
        return Result::emit(AppEvent{AdjustSwing{encoderInput->delta}});
    case InputId::VolumeEncoder:
        return Result::emit(AppEvent{AdjustVolume{encoderInput->delta}});
    }

    return Result::pass();
}

} // namespace SwingMetro
