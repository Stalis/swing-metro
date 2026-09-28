#pragma once

#include "engine/midi_clock_mode.h"

#include <cstdint>
#include <optional>

namespace SwingMetro {

class MidiClockModal {
  public:
    auto open(MidiClockMode mode) noexcept -> void {
        _open = true;
        _selection = static_cast<MidiClockMenuItem>(mode);
    }

    auto close() noexcept -> void { _open = false; }

    auto adjustPreview(std::int8_t delta) noexcept -> void {
        if (!_open) {
            return;
        }

        const auto candidate = static_cast<std::int16_t>(_selection) + delta;
        if (candidate <= static_cast<std::int16_t>(MidiClockMenuItem::Off)) {
            _selection = MidiClockMenuItem::Off;
        } else if (candidate >= static_cast<std::int16_t>(MidiClockMenuItem::Cancel)) {
            _selection = MidiClockMenuItem::Cancel;
        } else {
            _selection = static_cast<MidiClockMenuItem>(candidate);
        }
    }

    [[nodiscard]] auto isOpen() const noexcept -> bool { return _open; }
    [[nodiscard]] auto selection() const noexcept -> MidiClockMenuItem { return _selection; }
    [[nodiscard]] auto confirmedMode() const noexcept -> std::optional<MidiClockMode> {
        if (!_open || _selection == MidiClockMenuItem::Cancel) {
            return std::nullopt;
        }
        return static_cast<MidiClockMode>(_selection);
    }

  private:
    bool _open = false;
    MidiClockMenuItem _selection = MidiClockMenuItem::Off;
};

} // namespace SwingMetro
