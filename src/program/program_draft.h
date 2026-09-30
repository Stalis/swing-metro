#pragma once

#include "program/program.h"
#include "program/program_id.h"

#include <optional>

namespace SwingMetro {

class ProgramDraft {
  public:
    [[nodiscard]] auto load(const Program& program, std::optional<ProgramId> sourceId) -> bool;

    [[nodiscard]] auto program() -> Program& { return _program; }
    [[nodiscard]] auto program() const -> const Program& { return _program; }
    [[nodiscard]] auto sourceId() const -> std::optional<ProgramId> { return _sourceId; }

  private:
    Program _program{};
    std::optional<ProgramId> _sourceId;
};

} // namespace SwingMetro
