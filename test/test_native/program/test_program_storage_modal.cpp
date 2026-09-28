#include "test_program_storage_modal.h"

#include "program/program_storage_modal.h"

#include <unity.h>

namespace {

void test_modal_defaults_and_clamps_selection() {
    SwingMetro::ProgramStorageModal modal;
    modal.open();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageModalState::Action),
                            static_cast<std::uint8_t>(modal.snapshot().state));

    modal.selectAction(-127);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageMenuItem::Save),
                            static_cast<std::uint8_t>(modal.snapshot().selection));
    modal.selectAction(127);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStorageMenuItem::ResetProgram),
        static_cast<std::uint8_t>(modal.snapshot().selection));
}

void test_modal_emits_slot_commands_only_after_confirmation() {
    SwingMetro::ProgramStorageModal modal;
    modal.open();
    TEST_ASSERT_FALSE(modal.confirmSlot().has_value());
    TEST_ASSERT_TRUE(modal.confirmAction());
    modal.selectSlot(127);
    const auto command = modal.confirmSlot();

    TEST_ASSERT_TRUE(command.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageOperation::Save),
                            static_cast<std::uint8_t>(command->operation));
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_USER_SLOT_COUNT - 1, command->slot);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageModalState::Busy),
                            static_cast<std::uint8_t>(modal.snapshot().state));
    modal.complete(SwingMetro::ProgramStoreStatus::NotMounted);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageModalState::Error),
                            static_cast<std::uint8_t>(modal.snapshot().state));

    modal.open();
    modal.selectAction(1);
    TEST_ASSERT_TRUE(modal.confirmAction());
    const auto loadCommand = modal.confirmSlot();
    TEST_ASSERT_TRUE(loadCommand.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageOperation::Load),
                            static_cast<std::uint8_t>(loadCommand->operation));
}

void test_modal_cancel_and_reset_no_do_not_emit_commands() {
    SwingMetro::ProgramStorageModal modal;
    modal.open();
    modal.selectAction(2);
    TEST_ASSERT_FALSE(modal.confirmAction());
    TEST_ASSERT_TRUE(modal.closeRequested());
    modal.close();

    modal.open();
    TEST_ASSERT_TRUE(modal.confirmAction());
    modal.selectSlot(-1);
    TEST_ASSERT_FALSE(modal.confirmSlot().has_value());
    TEST_ASSERT_TRUE(modal.closeRequested());
    modal.close();

    modal.open();
    modal.selectAction(3);
    TEST_ASSERT_TRUE(modal.confirmAction());
    TEST_ASSERT_FALSE(modal.confirmReset().has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageModalState::Action),
                            static_cast<std::uint8_t>(modal.snapshot().state));

    modal.selectAction(3);
    TEST_ASSERT_TRUE(modal.confirmAction());
    modal.selectResetChoice(1);
    const auto command = modal.confirmReset();
    TEST_ASSERT_TRUE(command.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageOperation::Reset),
                            static_cast<std::uint8_t>(command->operation));
}

} // namespace

void testProgramStorageModalMain() {
    RUN_TEST(test_modal_defaults_and_clamps_selection);
    RUN_TEST(test_modal_emits_slot_commands_only_after_confirmation);
    RUN_TEST(test_modal_cancel_and_reset_no_do_not_emit_commands);
}
