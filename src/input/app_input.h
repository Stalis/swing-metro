#pragma once

#include "engine/sequencer.h"

#include <context_input.h>
#include <cstdint>
#include <optional>
#include <variant>

namespace SwingMetro {

enum class InputId : std::uint8_t {
    TempoEncoder,
    SwingEncoder,
    VolumeEncoder,
    TempoSwitch,
    ShiftSwitch,
    Step0 = 16,
    Step1,
    Step2,
    Step3,
    Step4,
    Step5,
    Step6,
    Step7,
    Step8,
    Step9,
    Step10,
    Step11,
    Step12,
    Step13,
    Step14,
    Step15,
};

[[nodiscard]] constexpr auto inputIdForStep(std::uint8_t step) noexcept -> InputId {
    return static_cast<InputId>(static_cast<std::uint8_t>(InputId::Step0) + step);
}

[[nodiscard]] constexpr auto stepIndexFromInputId(InputId source) noexcept
    -> std::optional<std::uint8_t> {
    const auto value = static_cast<std::uint8_t>(source);
    const auto first = static_cast<std::uint8_t>(InputId::Step0);
    const auto last = static_cast<std::uint8_t>(InputId::Step15);
    if (value < first || value > last) {
        return std::nullopt;
    }

    return static_cast<std::uint8_t>(value - first);
}

static_assert(static_cast<std::uint8_t>(InputId::Step15) -
                  static_cast<std::uint8_t>(InputId::Step0) + 1 ==
              STEPS_COUNT);

struct AdjustTempo {
    std::int8_t delta;
};

struct AdjustSwing {
    std::int8_t delta;
};

struct AdjustVolume {
    std::int8_t delta;
};

struct ToggleStep {
    std::uint8_t step;
};

struct OpenStepSettings {
    std::uint8_t step;
};

struct CloseStepSettings {};

struct AdjustNote {
    std::int8_t delta;
};

struct ToggleTransport {};
struct ActivateShift {};
struct DeactivateShift {};

using InputEvent = ContextInput::InputEvent<InputId>;
using AppEvent =
    std::variant<AdjustTempo, AdjustSwing, AdjustVolume, ToggleStep, OpenStepSettings,
                 CloseStepSettings, AdjustNote, ToggleTransport, ActivateShift, DeactivateShift>;

} // namespace SwingMetro
