#pragma once

#include <cstdint>

namespace SwingMetro {

enum class ProgramStorageAction : std::uint8_t { Save, Load };
enum class ProgramStorageModalState : std::uint8_t { Closed, Action, Slot, Busy, Success, Error };

} // namespace SwingMetro
