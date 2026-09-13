#include "test_program_slot_store.h"

#include "program/erased_flash.h"
#include "program/program_slot_store.h"

#include <array>
#include <cstring>
#include <limits>
#include <unity.h>

namespace {

using SwingMetro::ProgramStorageCopy;
using SwingMetro::ProgramStorageImage;
using SwingMetro::ProgramStorageReadResult;
using SwingMetro::ProgramStoreStatus;

constexpr std::size_t copyIndex(ProgramStorageCopy copy) {
    return copy == ProgramStorageCopy::A ? 0 : 1;
}

struct FakeStorage final : SwingMetro::ProgramStorageBackend {
    std::array<std::array<ProgramStorageImage, 2>, SwingMetro::PROGRAM_SLOT_COUNT> images{};
    std::array<std::array<bool, 2>, SwingMetro::PROGRAM_SLOT_COUNT> present{};
    std::array<std::array<bool, 2>, SwingMetro::PROGRAM_SLOT_COUNT> readFails{};
    bool mountSucceeds = true;
    bool writeFails = false;
    bool truncateNextWrite = false;
    bool alterNextWrite = false;

    auto mount() -> bool override { return mountSucceeds; }

    auto read(std::uint8_t slot, ProgramStorageCopy copy, ProgramStorageImage& image)
        -> ProgramStorageReadResult override {
        const auto index = copyIndex(copy);
        if (readFails[slot][index]) {
            return ProgramStorageReadResult::Failed;
        }
        if (!present[slot][index]) {
            return ProgramStorageReadResult::Missing;
        }
        image = images[slot][index];
        return ProgramStorageReadResult::Ok;
    }

    auto write(std::uint8_t slot, ProgramStorageCopy copy, const ProgramStorageImage& image)
        -> bool override {
        if (writeFails) {
            return false;
        }
        const auto index = copyIndex(copy);
        images[slot][index] = image;
        present[slot][index] = true;
        if (truncateNextWrite) {
            --images[slot][index].size;
            truncateNextWrite = false;
        }
        if (alterNextWrite) {
            images[slot][index].bytes[0] ^= 1;
            alterNextWrite = false;
        }
        return true;
    }
};

auto programWithVolume(std::uint8_t volume) -> SwingMetro::Program {
    SwingMetro::Program program;
    program.volume = volume;
    return program;
}

void seed(FakeStorage& storage, std::uint8_t slot, ProgramStorageCopy copy,
          const SwingMetro::Program& program, std::uint32_t revision) {
    const auto index = copyIndex(copy);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
                            static_cast<std::uint8_t>(SwingMetro::encodeProgram(
                                program, revision, storage.images[slot][index])));
    storage.present[slot][index] = true;
}

auto revisionOf(const ProgramStorageImage& image) -> std::uint32_t {
    SwingMetro::Program program;
    std::uint32_t revision = 0;
    (void)SwingMetro::decodeProgram(image.bytes.data(), image.size, program, revision);
    return revision;
}

void testMountAndSlotValidation() {
    FakeStorage storage;
    SwingMetro::ProgramSlotStore store(storage);
    SwingMetro::Program program;

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::NotMounted),
                            static_cast<std::uint8_t>(store.load(0, program)));
    storage.mountSucceeds = false;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::MountFailed),
                            static_cast<std::uint8_t>(store.mount()));
    storage.mountSucceeds = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.mount()));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(ProgramStoreStatus::InvalidSlot),
        static_cast<std::uint8_t>(store.load(SwingMetro::PROGRAM_SLOT_COUNT, program)));
}

void testErasedFlashRequiresEveryByteToBeErased() {
    constexpr std::array<std::uint8_t, 4> erased{UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX};
    constexpr std::array<std::uint8_t, 4> programmed{UINT8_MAX, UINT8_MAX, 0x00, UINT8_MAX};

    TEST_ASSERT_TRUE(SwingMetro::isErasedFlash(erased.data(), erased.size()));
    TEST_ASSERT_FALSE(SwingMetro::isErasedFlash(programmed.data(), programmed.size()));
}

void testSaveAndLoadAllUserSlotsAndCurrentProgram() {
    FakeStorage storage;
    SwingMetro::ProgramSlotStore store(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.mount()));

    for (std::uint8_t slot = 0; slot < SwingMetro::PROGRAM_SLOT_COUNT; ++slot) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
            static_cast<std::uint8_t>(store.save(slot, programWithVolume(slot))));
    }
    for (std::uint8_t slot = 0; slot < SwingMetro::PROGRAM_SLOT_COUNT; ++slot) {
        SwingMetro::Program loaded;
        TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                                static_cast<std::uint8_t>(store.load(slot, loaded)));
        TEST_ASSERT_EQUAL_UINT8(slot, loaded.volume);
        TEST_ASSERT_TRUE(storage.present[slot][copyIndex(ProgramStorageCopy::A)]);
        TEST_ASSERT_FALSE(storage.present[slot][copyIndex(ProgramStorageCopy::B)]);
    }
}

void testSaveAlternatesCopiesAndRevisionsWithoutTouchingOtherSlots() {
    FakeStorage storage;
    SwingMetro::ProgramSlotStore store(storage);
    (void)store.mount();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.save(0, programWithVolume(10))));
    const auto untouched = storage.images[0][copyIndex(ProgramStorageCopy::A)];
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.save(0, programWithVolume(20))));
    TEST_ASSERT_EQUAL_UINT32(1, revisionOf(storage.images[0][copyIndex(ProgramStorageCopy::A)]));
    TEST_ASSERT_EQUAL_UINT32(2, revisionOf(storage.images[0][copyIndex(ProgramStorageCopy::B)]));
    TEST_ASSERT_EQUAL_MEMORY(untouched.bytes.data(),
                             storage.images[0][copyIndex(ProgramStorageCopy::A)].bytes.data(),
                             untouched.size);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.save(1, programWithVolume(30))));
    TEST_ASSERT_TRUE(storage.present[1][copyIndex(ProgramStorageCopy::A)]);
    TEST_ASSERT_FALSE(storage.present[1][copyIndex(ProgramStorageCopy::B)]);
}

void testLoadChoosesNewerCopyAndFallsBackFromCorruptNewerCopy() {
    FakeStorage storage;
    seed(storage, 0, ProgramStorageCopy::A, programWithVolume(11), 1);
    seed(storage, 0, ProgramStorageCopy::B, programWithVolume(22), 2);
    SwingMetro::ProgramSlotStore store(storage);
    (void)store.mount();
    SwingMetro::Program loaded;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.load(0, loaded)));
    TEST_ASSERT_EQUAL_UINT8(22, loaded.volume);

    storage.images[0][copyIndex(ProgramStorageCopy::B)].bytes[0] ^= 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.load(0, loaded)));
    TEST_ASSERT_EQUAL_UINT8(11, loaded.volume);
}

void testInterruptedOrFailedWritePreservesPreviousCopy() {
    FakeStorage storage;
    SwingMetro::ProgramSlotStore store(storage);
    (void)store.mount();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.save(0, programWithVolume(10))));
    storage.truncateNextWrite = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::VerificationFailed),
                            static_cast<std::uint8_t>(store.save(0, programWithVolume(20))));
    SwingMetro::Program loaded;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.load(0, loaded)));
    TEST_ASSERT_EQUAL_UINT8(10, loaded.volume);

    storage.writeFails = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::WriteFailed),
                            static_cast<std::uint8_t>(store.save(0, programWithVolume(30))));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.load(0, loaded)));
    TEST_ASSERT_EQUAL_UINT8(10, loaded.volume);
}

void testEmptyCorruptReadFailureAndEqualRevisionConflict() {
    FakeStorage storage;
    SwingMetro::ProgramSlotStore store(storage);
    (void)store.mount();
    SwingMetro::Program loaded;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Empty),
                            static_cast<std::uint8_t>(store.load(0, loaded)));

    storage.present[0][copyIndex(ProgramStorageCopy::A)] = true;
    storage.images[0][copyIndex(ProgramStorageCopy::A)].size = 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Corrupt),
                            static_cast<std::uint8_t>(store.load(0, loaded)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.save(0, programWithVolume(30))));
    TEST_ASSERT_EQUAL_UINT32(1, revisionOf(storage.images[0][copyIndex(ProgramStorageCopy::A)]));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(store.load(0, loaded)));
    TEST_ASSERT_EQUAL_UINT8(30, loaded.volume);

    storage.present[0][copyIndex(ProgramStorageCopy::A)] = false;
    storage.readFails[0][copyIndex(ProgramStorageCopy::A)] = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::ReadFailed),
                            static_cast<std::uint8_t>(store.load(0, loaded)));

    storage.readFails[0][copyIndex(ProgramStorageCopy::A)] = false;
    seed(storage, 0, ProgramStorageCopy::A, programWithVolume(10), 3);
    seed(storage, 0, ProgramStorageCopy::B, programWithVolume(20), 3);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Corrupt),
                            static_cast<std::uint8_t>(store.load(0, loaded)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::Corrupt),
                            static_cast<std::uint8_t>(store.save(0, programWithVolume(30))));
    TEST_ASSERT_EQUAL_UINT32(3, revisionOf(storage.images[0][copyIndex(ProgramStorageCopy::A)]));
    TEST_ASSERT_EQUAL_UINT32(3, revisionOf(storage.images[0][copyIndex(ProgramStorageCopy::B)]));
}

void testSaveRejectsReadFailureInvalidProgramAndExhaustedRevision() {
    FakeStorage storage;
    SwingMetro::ProgramSlotStore store(storage);
    (void)store.mount();
    storage.readFails[0][copyIndex(ProgramStorageCopy::A)] = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::ReadFailed),
                            static_cast<std::uint8_t>(store.save(0, programWithVolume(10))));
    storage.readFails[0][copyIndex(ProgramStorageCopy::A)] = false;
    auto invalid = programWithVolume(10);
    invalid.tempo = 39;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::InvalidProgram),
                            static_cast<std::uint8_t>(store.save(0, invalid)));
    seed(storage, 0, ProgramStorageCopy::A, programWithVolume(10),
         std::numeric_limits<std::uint32_t>::max());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProgramStoreStatus::RevisionExhausted),
                            static_cast<std::uint8_t>(store.save(0, programWithVolume(20))));
}

} // namespace

void testProgramSlotStoreMain() {
    RUN_TEST(testMountAndSlotValidation);
    RUN_TEST(testErasedFlashRequiresEveryByteToBeErased);
    RUN_TEST(testSaveAndLoadAllUserSlotsAndCurrentProgram);
    RUN_TEST(testSaveAlternatesCopiesAndRevisionsWithoutTouchingOtherSlots);
    RUN_TEST(testLoadChoosesNewerCopyAndFallsBackFromCorruptNewerCopy);
    RUN_TEST(testInterruptedOrFailedWritePreservesPreviousCopy);
    RUN_TEST(testEmptyCorruptReadFailureAndEqualRevisionConflict);
    RUN_TEST(testSaveRejectsReadFailureInvalidProgramAndExhaustedRevision);
}
