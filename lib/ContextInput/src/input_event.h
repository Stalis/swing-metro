#pragma once

#include <cstdint>
#include <variant>

namespace ContextInput {

struct EncoderInput {
    std::int8_t delta;
};

enum class ButtonPhase : std::uint8_t {
    Pressed,
    Released,
    Clicked,
    LongPressed,
};

struct ButtonInput {
    ButtonPhase phase;
};

struct TriggerInput {
    bool active;
};

using InputPayload = std::variant<EncoderInput, ButtonInput, TriggerInput>;

template <typename TSourceId>
struct InputEvent {
    TSourceId source;
    InputPayload payload;
};

} // namespace ContextInput
