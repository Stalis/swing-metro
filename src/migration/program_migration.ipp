#include "program/program_migration.h"

#include <cstring>

namespace {

constexpr std::size_t VERSION_OFFSET = 0;
constexpr std::size_t RECORD_OFFSET = 1;

constexpr auto copyIndex(SwingMetro::ProgramStorageCopy copy) -> std::size_t {
    return copy == SwingMetro::ProgramStorageCopy::A ? 0 : 1;
}

constexpr auto recordOffset(std::uint8_t slot, SwingMetro::ProgramStorageCopy copy) -> std::size_t {
    return RECORD_OFFSET +
           (static_cast<std::size_t>(slot) * SwingMetro::PROGRAM_STORAGE_COPY_COUNT +
            copyIndex(copy)) *
               SwingMetro::PROGRAM_MIGRATION_COPY_SIZE;
}

auto read16(const std::uint8_t* data) -> std::uint16_t {
    return static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8);
}

void write16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
}

auto imagesMatch(const SwingMetro::ProgramStorageImage& first,
                 const SwingMetro::ProgramStorageImage& second) -> bool {
    return first.size == second.size &&
           std::memcmp(first.bytes.data(), second.bytes.data(), first.size) == 0;
}

} // namespace

namespace SwingMetro {

ProgramMigrationController::ProgramMigrationController(ProgramStorageBackend& storage) noexcept
    : _storage(storage) {}

auto ProgramMigrationController::exportBackup(ProgramMigrationBackup& backup)
    -> ProgramMigrationStatus {
    if (!_storage.mount()) {
        return ProgramMigrationStatus::MountFailed;
    }
    backup = {};
    backup.bytes[VERSION_OFFSET] = PROGRAM_MIGRATION_VERSION;
    for (std::uint8_t slot = 0; slot < PROGRAM_SLOT_COUNT; ++slot) {
        for (const auto copy : {ProgramStorageCopy::A, ProgramStorageCopy::B}) {
            const auto offset = recordOffset(slot, copy);
            ProgramStorageImage image{};
            const auto result = _storage.read(slot, copy, image);
            if (result == ProgramStorageReadResult::Missing) {
                continue;
            }
            if (result != ProgramStorageReadResult::Ok || image.size > image.bytes.size()) {
                return ProgramMigrationStatus::ReadFailed;
            }
            backup.bytes[offset] = 1;
            write16(backup.bytes.data() + offset + 1, static_cast<std::uint16_t>(image.size));
            std::memcpy(backup.bytes.data() + offset + 3, image.bytes.data(), image.size);
        }
    }
    return ProgramMigrationStatus::Ok;
}

auto ProgramMigrationController::validate(const ProgramMigrationBackup& backup) -> bool {
    if (backup.bytes[VERSION_OFFSET] != PROGRAM_MIGRATION_VERSION) {
        return false;
    }
    for (std::uint8_t slot = 0; slot < PROGRAM_SLOT_COUNT; ++slot) {
        for (const auto copy : {ProgramStorageCopy::A, ProgramStorageCopy::B}) {
            const auto offset = recordOffset(slot, copy);
            if (backup.bytes[offset] > 1 ||
                read16(backup.bytes.data() + offset + 1) > PROGRAM_MAX_ENCODED_SIZE) {
                return false;
            }
        }
    }
    return true;
}

auto ProgramMigrationController::restoreBackup(const ProgramMigrationBackup& backup)
    -> ProgramMigrationStatus {
    if (!validate(backup)) {
        return ProgramMigrationStatus::InvalidBackup;
    }
    if (!_storage.mount()) {
        return ProgramMigrationStatus::MountFailed;
    }
    for (std::uint8_t slot = 0; slot < PROGRAM_SLOT_COUNT; ++slot) {
        for (const auto copy : {ProgramStorageCopy::A, ProgramStorageCopy::B}) {
            ProgramStorageImage ignored{};
            if (_storage.read(slot, copy, ignored) != ProgramStorageReadResult::Missing) {
                return ProgramMigrationStatus::TargetNotEmpty;
            }
        }
    }
    for (std::uint8_t slot = 0; slot < PROGRAM_SLOT_COUNT; ++slot) {
        for (const auto copy : {ProgramStorageCopy::A, ProgramStorageCopy::B}) {
            const auto offset = recordOffset(slot, copy);
            if (backup.bytes[offset] == 0) {
                continue;
            }
            ProgramStorageImage image{};
            image.size = read16(backup.bytes.data() + offset + 1);
            std::memcpy(image.bytes.data(), backup.bytes.data() + offset + 3, image.size);
            if (!_storage.write(slot, copy, image)) {
                return ProgramMigrationStatus::WriteFailed;
            }
            ProgramStorageImage verified{};
            if (_storage.read(slot, copy, verified) != ProgramStorageReadResult::Ok ||
                !imagesMatch(image, verified)) {
                return ProgramMigrationStatus::VerificationFailed;
            }
        }
    }
    return ProgramMigrationStatus::Ok;
}

auto programMigrationCrc32(const std::uint8_t* data, std::size_t size) -> std::uint32_t {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & static_cast<std::uint32_t>(-(crc & 1U)));
        }
    }
    return ~crc;
}

} // namespace SwingMetro
