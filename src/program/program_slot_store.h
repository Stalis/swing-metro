#pragma once

#include "program/program_storage.h"

namespace SwingMetro {

enum class ProgramStoreStatus : std::uint8_t {
    Ok,
    NotMounted,
    MountFailed,
    InvalidSlot,
    Empty,
    Corrupt,
    ReadFailed,
    InvalidProgram,
    WriteFailed,
    VerificationFailed,
    RevisionExhausted,
    TransportRunning,
};

class ProgramSlotStore {
  public:
    explicit ProgramSlotStore(ProgramStorageBackend& storage) noexcept;
    [[nodiscard]] auto mount() -> ProgramStoreStatus;
    [[nodiscard]] auto load(std::uint8_t slot, Program& program) -> ProgramStoreStatus;
    [[nodiscard]] auto save(std::uint8_t slot, const Program& program) -> ProgramStoreStatus;

  private:
    ProgramStorageBackend& _storage;
    bool _mounted = false;
};

} // namespace SwingMetro
