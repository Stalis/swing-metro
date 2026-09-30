#include "test_program_bank.h"

#include "program/program_bank.h"
#include "program/program_draft.h"

#include <unity.h>

namespace {

void testProgramIdAcceptsOnlyUserSlots() {
    const auto first = SwingMetro::ProgramId::fromSlot(0);
    const auto last = SwingMetro::ProgramId::fromSlot(15);

    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, first->slot());
    TEST_ASSERT_TRUE(last.has_value());
    TEST_ASSERT_EQUAL_UINT8(15, last->slot());
    TEST_ASSERT_FALSE(SwingMetro::ProgramId::fromSlot(16).has_value());
    TEST_ASSERT_FALSE(SwingMetro::ProgramId::fromSlot(UINT8_MAX).has_value());
}

void testProgramBankStoresProgramsAndRevisesEachSlotIndependently() {
    const auto first = *SwingMetro::ProgramId::fromSlot(0);
    const auto second = *SwingMetro::ProgramId::fromSlot(1);
    SwingMetro::ProgramBank bank;
    SwingMetro::Program firstProgram;
    firstProgram.volume = 25;
    SwingMetro::Program replacement = firstProgram;
    replacement.volume = 75;
    SwingMetro::Program secondProgram;
    secondProgram.tempo = 180;

    TEST_ASSERT_NULL(bank.find(first));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramBankReplaceStatus::Ok),
                            static_cast<std::uint8_t>(bank.replace(first, firstProgram)));
    TEST_ASSERT_EQUAL_UINT32(1, bank.find(first)->revision);
    TEST_ASSERT_EQUAL_UINT8(25, bank.find(first)->program.volume);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramBankReplaceStatus::Ok),
                            static_cast<std::uint8_t>(bank.replace(second, secondProgram)));
    TEST_ASSERT_EQUAL_UINT32(1, bank.find(second)->revision);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramBankReplaceStatus::Ok),
                            static_cast<std::uint8_t>(bank.replace(first, replacement)));
    TEST_ASSERT_EQUAL_UINT32(2, bank.find(first)->revision);
    TEST_ASSERT_EQUAL_UINT8(75, bank.find(first)->program.volume);
    TEST_ASSERT_EQUAL_UINT32(1, bank.find(second)->revision);
    TEST_ASSERT_EQUAL_UINT8(180, bank.find(second)->program.tempo);
}

void testProgramBankRejectsInvalidReplacementWithoutChangingEntry() {
    const auto id = *SwingMetro::ProgramId::fromSlot(0);
    SwingMetro::ProgramBank bank;
    SwingMetro::Program saved;
    saved.volume = 60;
    SwingMetro::Program invalid = saved;
    invalid.tempo = 39;

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramBankReplaceStatus::Ok),
                            static_cast<std::uint8_t>(bank.replace(id, saved)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramBankReplaceStatus::InvalidProgram),
        static_cast<std::uint8_t>(bank.replace(id, invalid)));
    TEST_ASSERT_EQUAL_UINT32(1, bank.find(id)->revision);
    TEST_ASSERT_EQUAL_UINT8(60, bank.find(id)->program.volume);
}

void testProgramDraftCopiesBankProgramAndEditsDoNotMutateBank() {
    const auto id = *SwingMetro::ProgramId::fromSlot(3);
    SwingMetro::ProgramBank bank;
    SwingMetro::Program saved;
    saved.steps[0].enabled = true;
    saved.steps[0].note = 48;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramBankReplaceStatus::Ok),
                            static_cast<std::uint8_t>(bank.replace(id, saved)));

    SwingMetro::ProgramDraft draft;
    TEST_ASSERT_TRUE(draft.load(bank.find(id)->program, id));
    TEST_ASSERT_TRUE(draft.sourceId().has_value());
    TEST_ASSERT_EQUAL_UINT8(3, draft.sourceId()->slot());
    draft.program().steps[0].note = 72;
    draft.program().volume = 10;

    TEST_ASSERT_EQUAL_UINT8(48, bank.find(id)->program.steps[0].note);
    TEST_ASSERT_EQUAL_UINT8(100, bank.find(id)->program.volume);
    TEST_ASSERT_EQUAL_UINT8(72, draft.program().steps[0].note);
    TEST_ASSERT_EQUAL_UINT8(10, draft.program().volume);
}

void testProgramDraftRejectsInvalidLoadWithoutReplacingCurrentDraft() {
    SwingMetro::ProgramDraft draft;
    SwingMetro::Program saved;
    saved.tempo = 140;
    const auto sourceId = *SwingMetro::ProgramId::fromSlot(2);
    TEST_ASSERT_TRUE(draft.load(saved, sourceId));

    auto invalid = saved;
    invalid.steps[0].gate = 0;
    TEST_ASSERT_FALSE(draft.load(invalid, std::nullopt));
    TEST_ASSERT_EQUAL_UINT8(140, draft.program().tempo);
    TEST_ASSERT_TRUE(draft.sourceId().has_value());
    TEST_ASSERT_EQUAL_UINT8(2, draft.sourceId()->slot());
}

} // namespace

void testProgramBankMain() {
    RUN_TEST(testProgramIdAcceptsOnlyUserSlots);
    RUN_TEST(testProgramBankStoresProgramsAndRevisesEachSlotIndependently);
    RUN_TEST(testProgramBankRejectsInvalidReplacementWithoutChangingEntry);
    RUN_TEST(testProgramDraftCopiesBankProgramAndEditsDoNotMutateBank);
    RUN_TEST(testProgramDraftRejectsInvalidLoadWithoutReplacingCurrentDraft);
}
