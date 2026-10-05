#pragma once

#include <cstdint>

namespace SwingMetro {

enum class UiStepDisplayState : std::uint8_t { Disabled, Off, Enabled, EnabledActive, OffActive };

[[nodiscard]] constexpr auto uiStepDisplayState(bool enabled, bool active,
                                                bool withinLength) noexcept -> UiStepDisplayState {
    if (!withinLength) {
        return UiStepDisplayState::Disabled;
    }
    if (!enabled) {
        return active ? UiStepDisplayState::OffActive : UiStepDisplayState::Off;
    }
    return active ? UiStepDisplayState::EnabledActive : UiStepDisplayState::Enabled;
}

} // namespace SwingMetro
