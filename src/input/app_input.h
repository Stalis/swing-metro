#pragma once

#include <context_input.h>
#include <cstdint>
#include <variant>

namespace SwingMetro {

enum class InputId : std::uint8_t {
    TempoEncoder,
    SwingEncoder,
    VolumeEncoder,
};

struct AdjustTempo {
    std::int8_t delta;
};

struct AdjustSwing {
    std::int8_t delta;
};

struct AdjustVolume {
    std::int8_t delta;
};

using InputEvent = ContextInput::InputEvent<InputId>;
using AppEvent = std::variant<AdjustTempo, AdjustSwing, AdjustVolume>;

} // namespace SwingMetro
