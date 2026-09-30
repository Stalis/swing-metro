#pragma once

#include "program/program.h"
#include "program/program_id.h"

#include <array>
#include <cstdint>
#include <optional>

namespace SwingMetro {

using ProgramRevision = std::uint32_t;

struct ProgramBankEntry {
    Program program;
    ProgramRevision revision;
};

enum class ProgramBankReplaceStatus : std::uint8_t { Ok, InvalidProgram, RevisionExhausted };

class ProgramBank {
  public:
    [[nodiscard]] auto find(ProgramId id) const noexcept -> const ProgramBankEntry*;
    [[nodiscard]] auto replace(ProgramId id, const Program& program) -> ProgramBankReplaceStatus;

  private:
    std::array<std::optional<ProgramBankEntry>, PROGRAM_USER_SLOT_COUNT> _entries{};
};

} // namespace SwingMetro
