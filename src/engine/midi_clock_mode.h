#pragma once

#include <cstdint>

namespace SwingMetro {

enum class MidiClockMode : std::uint8_t { Off, Internal, External };
enum class MidiClockMenuItem : std::uint8_t { Off, Internal, External, Cancel };

class MidiClockSettings {
  public:
    [[nodiscard]] constexpr auto mode() const noexcept -> MidiClockMode { return _mode; }
    constexpr auto apply(MidiClockMode mode) noexcept -> void { _mode = mode; }

  private:
    MidiClockMode _mode = MidiClockMode::Off;
};

} // namespace SwingMetro
