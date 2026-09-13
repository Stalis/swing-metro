#include "test_program_migration.h"

#include "program/program_migration.h"

#include <array>
#include <cstring>
#include <unity.h>

namespace {

constexpr auto testCopyIndex(SwingMetro::ProgramStorageCopy copy) -> std::size_t {
    return copy == SwingMetro::ProgramStorageCopy::A ? 0 : 1;
}

struct FakeStorage final : SwingMetro::ProgramStorageBackend {
    std::array<std::array<SwingMetro::ProgramStorageImage, 2>, SwingMetro::PROGRAM_SLOT_COUNT>
        images{};
    std::array<std::array<bool, 2>, SwingMetro::PROGRAM_SLOT_COUNT> present{};
    std::size_t writes = 0;

    auto mount() -> bool override { return true; }
    auto read(std::uint8_t slot, SwingMetro::ProgramStorageCopy copy,
              SwingMetro::ProgramStorageImage& image)
        -> SwingMetro::ProgramStorageReadResult override {
        if (!present[slot][testCopyIndex(copy)]) {
            return SwingMetro::ProgramStorageReadResult::Missing;
        }
        image = images[slot][testCopyIndex(copy)];
        return SwingMetro::ProgramStorageReadResult::Ok;
    }
    auto write(std::uint8_t slot, SwingMetro::ProgramStorageCopy copy,
               const SwingMetro::ProgramStorageImage& image) -> bool override {
        ++writes;
        images[slot][testCopyIndex(copy)] = image;
        present[slot][testCopyIndex(copy)] = true;
        return true;
    }
};

void testExportRestorePreservesEveryRawCopy() {
    FakeStorage source;
    source.present[0][0] = true;
    source.images[0][0].size = 3;
    source.images[0][0].bytes = {1, 2, 3};
    source.present[16][1] = true;
    source.images[16][1].size = 2;
    source.images[16][1].bytes = {0xAA, 0x55};
    SwingMetro::ProgramMigrationBackup backup;
    SwingMetro::ProgramMigrationController exporter(source);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramMigrationStatus::Ok),
                            static_cast<std::uint8_t>(exporter.exportBackup(backup)));

    FakeStorage target;
    SwingMetro::ProgramMigrationController importer(target);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramMigrationStatus::Ok),
                            static_cast<std::uint8_t>(importer.restoreBackup(backup)));
    TEST_ASSERT_TRUE(target.present[0][0]);
    TEST_ASSERT_FALSE(target.present[0][1]);
    TEST_ASSERT_TRUE(target.present[16][1]);
    TEST_ASSERT_EQUAL_UINT32(2, target.writes);
    TEST_ASSERT_EQUAL_MEMORY(source.images[0][0].bytes.data(), target.images[0][0].bytes.data(), 3);
    TEST_ASSERT_EQUAL_MEMORY(source.images[16][1].bytes.data(), target.images[16][1].bytes.data(),
                             2);
}

void testRestoreValidatesAndRejectsNonemptyBeforeWriting() {
    FakeStorage source;
    SwingMetro::ProgramMigrationBackup backup;
    SwingMetro::ProgramMigrationController exporter(source);
    (void)exporter.exportBackup(backup);
    backup.bytes[1] = 2;
    FakeStorage target;
    SwingMetro::ProgramMigrationController importer(target);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramMigrationStatus::InvalidBackup),
        static_cast<std::uint8_t>(importer.restoreBackup(backup)));
    TEST_ASSERT_EQUAL_UINT32(0, target.writes);

    (void)exporter.exportBackup(backup);
    target.present[1][1] = true;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramMigrationStatus::TargetNotEmpty),
        static_cast<std::uint8_t>(importer.restoreBackup(backup)));
    TEST_ASSERT_EQUAL_UINT32(0, target.writes);
}

} // namespace

void testProgramMigrationMain() {
    RUN_TEST(testExportRestorePreservesEveryRawCopy);
    RUN_TEST(testRestoreValidatesAndRejectsNonemptyBeforeWriting);
}
