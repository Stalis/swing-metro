#include "test_app_ui_snapshot_builder.h"

#include "input/app_ui_snapshot_builder.h"

#include <unity.h>

namespace {

void test_builder_preserves_main_and_decorates_all_snapshot_fields() {
    Sequencer sequencer;
    TEST_ASSERT_TRUE(sequencer.adjustStepNote(4, 7));
    TEST_ASSERT_TRUE(sequencer.adjustStepVelocity(4, -9));
    TEST_ASSERT_TRUE(sequencer.adjustStepGate(4, -11));
    sequencer.toggleRunning(0);
    SwingMetro::StepEditor stepEditor{sequencer};
    const SwingMetro::ProgramStorageModalSnapshot storage{
        .state = SwingMetro::ProgramStorageModalState::Busy,
        .selection = SwingMetro::ProgramStorageMenuItem::ResetProgram,
        .action = SwingMetro::ProgramStorageAction::Load,
        .slot = 4,
        .status = SwingMetro::ProgramStoreStatus::WriteFailed,
        .resetChoice = SwingMetro::ProgramResetChoice::Yes,
    };
    const auto settings = SwingMetro::AppUiSnapshotBuilder::decorate(
        {.main = {.tempo = 123,
                  .swing = 61,
                  .volume = 77,
                  .activeNote = 4,
                  .sequenceNumber = 3,
                  .midiChannelMask = 0x007F}},
        {.stepEditor = stepEditor,
         .selectedStep = 4,
         .shiftActive = true,
         .midiClockModalOpen = true,
         .midiClockActive = SwingMetro::MidiClockMode::External,
         .midiClockSelection = SwingMetro::MidiClockMenuItem::Cancel,
         .storage = storage});

    TEST_ASSERT_EQUAL_UINT8(123, settings.main.tempo);
    TEST_ASSERT_EQUAL_UINT8(61, settings.main.swing);
    TEST_ASSERT_EQUAL_UINT8(77, settings.main.volume);
    TEST_ASSERT_EQUAL_UINT8(4, settings.main.activeNote);
    TEST_ASSERT_EQUAL_UINT8(3, settings.main.sequenceNumber);
    TEST_ASSERT_EQUAL_UINT16(0x007F, settings.main.midiChannelMask);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiPage::StepSettings),
                            static_cast<std::uint8_t>(settings.page));
    TEST_ASSERT_EQUAL_UINT8(4, settings.editor.selectedStep);
    TEST_ASSERT_EQUAL_UINT8(43, settings.editor.selectedNote);
    TEST_ASSERT_EQUAL_UINT8(118, settings.editor.selectedVelocity);
    TEST_ASSERT_EQUAL_UINT8(89, settings.editor.selectedGate);
    TEST_ASSERT_TRUE(settings.editor.transportRunning);
    TEST_ASSERT_TRUE(settings.editor.shiftActive);
    TEST_ASSERT_TRUE(settings.midiClock.modalOpen);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(settings.midiClock.active));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMenuItem::Cancel),
                            static_cast<std::uint8_t>(settings.midiClock.selection));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageModalState::Busy),
                            static_cast<std::uint8_t>(settings.storage.state));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStorageMenuItem::ResetProgram),
        static_cast<std::uint8_t>(settings.storage.selection));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStorageAction::Load),
                            static_cast<std::uint8_t>(settings.storage.action));
    TEST_ASSERT_EQUAL_UINT8(4, settings.storage.slot);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::WriteFailed),
                            static_cast<std::uint8_t>(settings.storage.status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramResetChoice::Yes),
                            static_cast<std::uint8_t>(settings.storage.resetChoice));
}

} // namespace

void test_app_ui_snapshot_builder_main() {
    RUN_TEST(test_builder_preserves_main_and_decorates_all_snapshot_fields);
}
