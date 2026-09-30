#pragma once

#include "engine/session.h"
#include "program/program_runtime.h"
#include "program/program_slot_store.h"
#include "program/program_storage_modal.h"

namespace SwingMetro {

class ProgramStorageController {
  public:
    ProgramStorageController(ProgramSlotStore& store, Session& session,
                             Counter<std::uint8_t>& swing, Counter<std::uint8_t>& volume) noexcept;

    [[nodiscard]] auto perform(ProgramStorageAction action, std::uint8_t slot)
        -> ProgramStoreStatus;
    [[nodiscard]] auto resetCurrentProgram() -> ProgramStoreStatus;
    [[nodiscard]] auto restoreCurrentProgram() -> ProgramStoreStatus;
    [[nodiscard]] auto syncCurrentProgramIfChanged() -> ProgramStoreStatus;

  private:
    [[nodiscard]] auto saveCurrentProgram(const Program& program) -> ProgramStoreStatus;
    [[nodiscard]] auto currentProgramCrc(const Program& program) const -> std::uint32_t;
    auto rememberCurrentProgram(const Program& program) -> void;
    ProgramSlotStore& _store;
    Session& _session;
    Counter<std::uint8_t>& _swing;
    Counter<std::uint8_t>& _volume;
    std::uint32_t _currentProgramCrc = 0;
    bool _hasCurrentProgramCrc = false;
};

} // namespace SwingMetro
