#pragma once

#include "components/ui_snapshot.h"

#include <array>
#include <cstdint>
#include <optional>

namespace SwingMetro::UiMockData {

[[nodiscard]] constexpr auto sequenceNumber() -> std::uint8_t { return 1; }
[[nodiscard]] constexpr auto midiChannelMask() -> std::uint16_t { return 0xFFFF; }

[[nodiscard]] constexpr auto stepMode(std::optional<std::uint8_t> selectedStep) -> UiStepMode {
#if SWING_METRO_UI_PREVIEW
    if (selectedStep == 1) {
        return UiStepMode::Legato;
    }
    if (selectedStep == 2) {
        return UiStepMode::Repeat;
    }
#else
    (void)selectedStep;
#endif
    return UiStepMode::Normal;
}

[[nodiscard]] constexpr auto repeatCount() -> std::uint8_t { return 4; }

inline auto applyMainPreview(UiSettings::Main& main) -> void {
#if SWING_METRO_UI_PREVIEW
    main.tempo = 124;
    main.swing = 62;
    main.activeNote = 3;
    main.highlightedStep = 2;
    main.sequenceLength = 12;
    main.notesState = 0x0F7B;
    main.stepNotes = {61, 46, 36, 98, 65, 79, 63, 36, 81, 48, 78, 59, 36, 36, 36, 36};
    main.stepVelocities = {96,  127, 127, 112, 80,  64,  118, 127,
                           101, 72,  123, 55,  127, 127, 127, 127};
    main.stepGates = {75, 38, 100, 62, 90, 25, 50, 100, 100, 44, 81, 18, 100, 100, 100, 100};
#else
    (void)main;
#endif
}

} // namespace SwingMetro::UiMockData
