#pragma once

#include "program/program_codec.h"

#include <cstdint>

namespace SwingMetro {

constexpr std::uint8_t PROGRAM_USER_SLOT_COUNT = 16;
constexpr std::uint8_t PROGRAM_CURRENT_SLOT = PROGRAM_USER_SLOT_COUNT;
constexpr std::uint8_t PROGRAM_SLOT_COUNT = PROGRAM_CURRENT_SLOT + 1;

enum class ProgramStorageCopy : std::uint8_t { A, B };
enum class ProgramStorageReadResult : std::uint8_t { Ok, Missing, Failed };

using ProgramStorageImage = EncodedProgram;

class ProgramStorageBackend {
  public:
    virtual ~ProgramStorageBackend() = default;
    virtual auto mount() -> bool = 0;
    virtual auto read(std::uint8_t slot, ProgramStorageCopy copy, ProgramStorageImage& image)
        -> ProgramStorageReadResult = 0;
    virtual auto write(std::uint8_t slot, ProgramStorageCopy copy, const ProgramStorageImage& image)
        -> bool = 0;
};

[[nodiscard]] constexpr auto isValidProgramSlot(std::uint8_t slot) -> bool {
    return slot < PROGRAM_SLOT_COUNT;
}

} // namespace SwingMetro
