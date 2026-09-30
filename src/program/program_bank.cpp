#include "program/program_bank.h"

#include <limits>

namespace SwingMetro {

auto ProgramBank::find(ProgramId id) const noexcept -> const ProgramBankEntry* {
    const auto& entry = _entries[id.slot()];
    return entry.has_value() ? &entry.value() : nullptr;
}

auto ProgramBank::replace(ProgramId id, const Program& program) -> ProgramBankReplaceStatus {
    if (!isValid(program)) {
        return ProgramBankReplaceStatus::InvalidProgram;
    }

    auto& entry = _entries[id.slot()];
    ProgramRevision revision = 1;
    if (entry.has_value()) {
        if (entry->revision == std::numeric_limits<ProgramRevision>::max()) {
            return ProgramBankReplaceStatus::RevisionExhausted;
        }
        revision = entry->revision + 1;
    }

    entry = ProgramBankEntry{.program = program, .revision = revision};
    return ProgramBankReplaceStatus::Ok;
}

} // namespace SwingMetro
