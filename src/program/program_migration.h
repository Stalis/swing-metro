#pragma once

#include "program/program_storage.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

constexpr std::uint8_t PROGRAM_MIGRATION_VERSION = 1;
constexpr std::size_t PROGRAM_STORAGE_COPY_COUNT = 2;
constexpr std::size_t PROGRAM_MIGRATION_COPY_SIZE =
    1 + sizeof(std::uint16_t) + PROGRAM_MAX_ENCODED_SIZE;
constexpr std::size_t PROGRAM_MIGRATION_PAYLOAD_SIZE =
    1 + PROGRAM_SLOT_COUNT * PROGRAM_STORAGE_COPY_COUNT * PROGRAM_MIGRATION_COPY_SIZE;

enum class ProgramMigrationStatus : std::uint8_t {
    Ok,
    MountFailed,
    ReadFailed,
    InvalidBackup,
    TargetNotEmpty,
    WriteFailed,
    VerificationFailed,
};

struct ProgramMigrationBackup {
    std::array<std::uint8_t, PROGRAM_MIGRATION_PAYLOAD_SIZE> bytes{};
};

class ProgramMigrationController {
  public:
    explicit ProgramMigrationController(ProgramStorageBackend& storage) noexcept;
    [[nodiscard]] auto exportBackup(ProgramMigrationBackup& backup) -> ProgramMigrationStatus;
    [[nodiscard]] auto restoreBackup(const ProgramMigrationBackup& backup)
        -> ProgramMigrationStatus;
    [[nodiscard]] static auto validate(const ProgramMigrationBackup& backup) -> bool;

  private:
    ProgramStorageBackend& _storage;
};

[[nodiscard]] auto programMigrationCrc32(const std::uint8_t* data, std::size_t size)
    -> std::uint32_t;

} // namespace SwingMetro

#include "migration/program_migration.ipp"
