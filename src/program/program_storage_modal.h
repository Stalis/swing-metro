#pragma once

#include <cstdint>

namespace SwingMetro {

enum class ProgramStorageAction : std::uint8_t { Save, Load };
enum class ProgramStorageMenuItem : std::uint8_t { Save, Load, Cancel };
enum class ProgramStorageModalState : std::uint8_t { Closed, Action, Slot, Busy, Success, Error };
constexpr std::uint8_t PROGRAM_STORAGE_CANCEL_SLOT = UINT8_MAX;

} // namespace SwingMetro
