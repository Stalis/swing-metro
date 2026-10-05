#include "test_app_ui_snapshot_builder.h"

#include "input/app_ui_snapshot_builder.h"
#include "input/ui_step_display_state.h"

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
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiStepMode::Normal),
                            static_cast<std::uint8_t>(settings.editor.mode));
    TEST_ASSERT_EQUAL_UINT8(4, settings.editor.repeatCount);
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

void test_builder_tracks_live_step_and_clears_active_on_stop() {
    Sequencer sequencer;
    SwingMetro::StepEditor stepEditor{sequencer};
    const SwingMetro::ProgramStorageModalSnapshot storage{};
    SwingMetro::AppUiSnapshotSources sources{
        .stepEditor = stepEditor,
        .selectedStep = 1,
        .shiftActive = false,
        .midiClockModalOpen = false,
        .midiClockActive = SwingMetro::MidiClockMode::Off,
        .midiClockSelection = SwingMetro::MidiClockMenuItem::Off,
        .storage = storage,
    };
    std::uint8_t tempo = 137;
    auto snapshot = [&sources, &tempo](std::uint8_t activeStep) {
        return SwingMetro::AppUiSnapshotBuilder::decorate(
            {.main = {.tempo = tempo,
                      .swing = 57,
                      .volume = 90,
                      .activeNote = activeStep,
                      .notesState = std::bitset<16>(0x0003)}},
            sources);
    };

    sequencer.start();
    sequencer.notifyBoundaryReached(0);
    auto settings = snapshot(*sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(0, settings.main.activeNote);
    TEST_ASSERT_EQUAL_UINT8(137, settings.main.tempo);
    TEST_ASSERT_EQUAL_UINT8(57, settings.main.swing);
    TEST_ASSERT_TRUE(settings.main.notesState[0]);
    TEST_ASSERT_TRUE(settings.main.notesState[1]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiStepMode::Normal),
                            static_cast<std::uint8_t>(settings.editor.mode));
    sources.selectedStep = 2;
    settings = snapshot(*sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiStepMode::Normal),
                            static_cast<std::uint8_t>(settings.editor.mode));

    sequencer.notifyBoundaryReached(SwingMetro::TICKS_PER_SIXTEENTH);
    settings = snapshot(*sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(1, settings.main.activeNote);

    (void)sequencer.stop();
    settings = snapshot(*sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, settings.main.activeNote);
    TEST_ASSERT_EQUAL_UINT8(137, settings.main.tempo);
    TEST_ASSERT_EQUAL_UINT8(57, settings.main.swing);

    tempo = 142;
    settings = snapshot(*sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(142, settings.main.tempo);

    sequencer.start();
    settings = snapshot(sequencer.getDisplayStepIndex().value_or(UINT8_MAX));
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, settings.main.activeNote);
    sequencer.notifyBoundaryReached(0);
    settings = snapshot(*sequencer.getDisplayStepIndex());
    TEST_ASSERT_EQUAL_UINT8(0, settings.main.activeNote);
}

void test_step_display_state_uses_live_enabled_active_and_length() {
    using SwingMetro::UiStepDisplayState;
    using SwingMetro::uiStepDisplayState;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiStepDisplayState::Enabled),
                            static_cast<std::uint8_t>(uiStepDisplayState(true, false, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiStepDisplayState::EnabledActive),
                            static_cast<std::uint8_t>(uiStepDisplayState(true, true, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiStepDisplayState::Off),
                            static_cast<std::uint8_t>(uiStepDisplayState(false, false, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiStepDisplayState::Off),
                            static_cast<std::uint8_t>(uiStepDisplayState(false, true, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(UiStepDisplayState::Disabled),
                            static_cast<std::uint8_t>(uiStepDisplayState(true, true, false)));
}

} // namespace

void test_app_ui_snapshot_builder_main() {
    RUN_TEST(test_builder_preserves_main_and_decorates_all_snapshot_fields);
    RUN_TEST(test_builder_tracks_live_step_and_clears_active_on_stop);
    RUN_TEST(test_step_display_state_uses_live_enabled_active_and_length);
}
