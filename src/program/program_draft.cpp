#include "program/program_draft.h"

namespace SwingMetro {

auto ProgramDraft::load(const Program& program, std::optional<ProgramId> sourceId) -> bool {
    if (!isValid(program)) {
        return false;
    }

    _program = program;
    _sourceId = sourceId;
    return true;
}

} // namespace SwingMetro
