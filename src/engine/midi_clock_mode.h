#pragma once

#include <cstdint>

namespace SwingMetro {

enum class MidiClockMode : std::uint8_t { Off, Internal, External };

class MidiClockSettings {
  public:
    [[nodiscard]] constexpr auto mode() const noexcept -> MidiClockMode { return mode_; }
    constexpr auto apply(MidiClockMode mode) noexcept -> void { mode_ = mode; }

  private:
    MidiClockMode mode_ = MidiClockMode::Off;
};

} // namespace SwingMetro
