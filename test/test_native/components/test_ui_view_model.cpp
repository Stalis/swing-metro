#include <thread>
#include <unity.h>

#include "components/ui_view_model.h"

namespace {

UiSettings firstSettings() {
    return {.main = {.tempo = 120,
                     .swing = 50,
                     .volume = 100,
                     .activeNote = 1,
                     .notesState = std::bitset<16>(0x0001),
                     .externalClockStatus = SwingMetro::ExternalMidiClockStatus::Waiting,
                     .externalTempo = 0},
            .editor = {.selectedStep = UINT8_MAX,
                       .selectedNote = 36,
                       .selectedVelocity = 127,
                       .selectedGate = 100,
                       .transportRunning = true,
                       .shiftActive = false},
            .midiClock = {.modalOpen = false,
                          .active = SwingMetro::MidiClockMode::Off,
                          .selection = SwingMetro::MidiClockMenuItem::Off},
            .storage = {.state = SwingMetro::ProgramStorageModalState::Closed,
                        .selection = SwingMetro::ProgramStorageMenuItem::Save,
                        .action = SwingMetro::ProgramStorageAction::Save,
                        .slot = 0,
                        .resetChoice = SwingMetro::ProgramResetChoice::No,
                        .status = SwingMetro::ProgramStoreStatus::Ok},
            .page = UiPage::MainDisplay};
}

UiSettings initialSettings() {
    return {.main = {.tempo = 0,
                     .swing = 0,
                     .volume = 0,
                     .activeNote = 0,
                     .notesState = {},
                     .externalClockStatus = SwingMetro::ExternalMidiClockStatus::Waiting,
                     .externalTempo = 0},
            .editor = {.selectedStep = 0,
                       .selectedNote = 0,
                       .selectedVelocity = 0,
                       .selectedGate = 100,
                       .transportRunning = false,
                       .shiftActive = false},
            .midiClock = {.modalOpen = false,
                          .active = SwingMetro::MidiClockMode::Off,
                          .selection = SwingMetro::MidiClockMenuItem::Off},
            .storage = {.state = SwingMetro::ProgramStorageModalState::Closed,
                        .selection = SwingMetro::ProgramStorageMenuItem::Save,
                        .action = SwingMetro::ProgramStorageAction::Save,
                        .slot = 0,
                        .resetChoice = SwingMetro::ProgramResetChoice::No,
                        .status = SwingMetro::ProgramStoreStatus::Ok},
            .page = UiPage::MainDisplay};
}

UiSettings secondSettings() {
    return {.main = {.tempo = 180,
                     .swing = 75,
                     .volume = 25,
                     .activeNote = 12,
                     .notesState = std::bitset<16>(0xF000),
                     .externalClockStatus = SwingMetro::ExternalMidiClockStatus::Lost,
                     .externalTempo = 199},
            .editor = {.selectedStep = 12,
                       .selectedNote = 61,
                       .selectedVelocity = 64,
                       .selectedGate = 25,
                       .transportRunning = false,
                       .shiftActive = true},
            .midiClock = {.modalOpen = true,
                          .active = SwingMetro::MidiClockMode::External,
                          .selection = SwingMetro::MidiClockMenuItem::Cancel},
            .storage = {.state = SwingMetro::ProgramStorageModalState::Error,
                        .selection = SwingMetro::ProgramStorageMenuItem::ResetProgram,
                        .action = SwingMetro::ProgramStorageAction::Load,
                        .slot = SwingMetro::PROGRAM_STORAGE_CANCEL_SLOT,
                        .resetChoice = SwingMetro::ProgramResetChoice::Yes,
                        .status = SwingMetro::ProgramStoreStatus::Empty},
            .page = UiPage::StepSettings};
}

bool sameSettings(const UiSettings& left, const UiSettings& right) {
    return left.main.tempo == right.main.tempo && left.main.swing == right.main.swing &&
           left.main.volume == right.main.volume && left.main.activeNote == right.main.activeNote &&
           left.main.notesState == right.main.notesState &&
           left.main.externalClockStatus == right.main.externalClockStatus &&
           left.main.externalTempo == right.main.externalTempo &&
           left.editor.selectedStep == right.editor.selectedStep &&
           left.editor.selectedNote == right.editor.selectedNote &&
           left.editor.selectedVelocity == right.editor.selectedVelocity &&
           left.editor.selectedGate == right.editor.selectedGate &&
           left.editor.transportRunning == right.editor.transportRunning &&
           left.editor.shiftActive == right.editor.shiftActive &&
           left.midiClock.modalOpen == right.midiClock.modalOpen &&
           left.midiClock.active == right.midiClock.active &&
           left.midiClock.selection == right.midiClock.selection &&
           left.storage.state == right.storage.state &&
           left.storage.selection == right.storage.selection &&
           left.storage.action == right.storage.action && left.storage.slot == right.storage.slot &&
           left.storage.resetChoice == right.storage.resetChoice &&
           left.storage.status == right.storage.status && left.page == right.page;
}

} // namespace

void test_ui_view_model_returns_initial_snapshot_defaults() {
    UiViewModel viewModel;
    TEST_ASSERT_TRUE(sameSettings(initialSettings(), viewModel.read()));
}

void test_ui_view_model_roundtrips_all_fields() {
    UiViewModel viewModel;
    const auto settings = secondSettings();
    viewModel.publish(settings);
    TEST_ASSERT_TRUE(sameSettings(settings, viewModel.read()));
}

void test_ui_view_model_publishes_each_field_change() {
    UiViewModel viewModel;
    auto baseline = firstSettings();

    const auto assertChange = [&viewModel, &baseline](const UiSettings& settings) {
        viewModel.publish(baseline);
        TEST_ASSERT_TRUE(sameSettings(baseline, viewModel.read()));
        viewModel.publish(settings);
        TEST_ASSERT_TRUE(sameSettings(settings, viewModel.read()));
    };

    auto changed = baseline;
    changed.main.tempo = 121;
    assertChange(changed);
    changed = baseline;
    changed.main.swing = 51;
    assertChange(changed);
    changed = baseline;
    changed.main.volume = 99;
    assertChange(changed);
    changed = baseline;
    changed.main.activeNote = UINT8_MAX;
    assertChange(changed);
    changed = baseline;
    changed.main.notesState = std::bitset<16>(0x8000);
    assertChange(changed);
    changed = baseline;
    changed.editor.transportRunning = false;
    assertChange(changed);
    changed = baseline;
    changed.editor.shiftActive = true;
    assertChange(changed);
    changed = baseline;
    changed.main.externalClockStatus = SwingMetro::ExternalMidiClockStatus::Locked;
    assertChange(changed);
    changed = baseline;
    changed.main.externalTempo = 123;
    assertChange(changed);
    changed = baseline;
    changed.editor.selectedStep = 7;
    assertChange(changed);
    changed = baseline;
    changed.editor.selectedNote = 49;
    assertChange(changed);
    changed = baseline;
    changed.editor.selectedVelocity = 96;
    assertChange(changed);
    changed = baseline;
    changed.editor.selectedGate = 75;
    assertChange(changed);
    changed = baseline;
    changed.midiClock.modalOpen = true;
    assertChange(changed);
    changed = baseline;
    changed.midiClock.active = SwingMetro::MidiClockMode::Internal;
    assertChange(changed);
    changed = baseline;
    changed.midiClock.selection = SwingMetro::MidiClockMenuItem::Cancel;
    assertChange(changed);
    changed = baseline;
    changed.storage.state = SwingMetro::ProgramStorageModalState::Slot;
    assertChange(changed);
    changed = baseline;
    changed.storage.selection = SwingMetro::ProgramStorageMenuItem::ResetProgram;
    assertChange(changed);
    changed = baseline;
    changed.storage.action = SwingMetro::ProgramStorageAction::Load;
    assertChange(changed);
    changed = baseline;
    changed.storage.slot = SwingMetro::PROGRAM_STORAGE_CANCEL_SLOT;
    assertChange(changed);
    changed = baseline;
    changed.storage.resetChoice = SwingMetro::ProgramResetChoice::Yes;
    assertChange(changed);
    changed = baseline;
    changed.storage.status = SwingMetro::ProgramStoreStatus::Empty;
    assertChange(changed);
    changed = baseline;
    changed.page = UiPage::StepSettings;
    assertChange(changed);
}

void test_ui_view_model_reads_complete_concurrent_snapshots() {
    UiViewModel viewModel;
    const auto first = firstSettings();
    const auto second = secondSettings();
    viewModel.publish(first);

    std::thread writer([&viewModel, &first, &second]() {
        for (int index = 0; index < 20000; ++index) {
            viewModel.publish(index % 2 == 0 ? second : first);
        }
    });

    bool coherent = true;
    for (int index = 0; index < 20000; ++index) {
        const auto value = viewModel.read();
        if (!sameSettings(value, first) && !sameSettings(value, second)) {
            coherent = false;
            break;
        }
    }
    writer.join();
    TEST_ASSERT_TRUE(coherent);
}

void test_ui_view_model_main() {
    RUN_TEST(test_ui_view_model_returns_initial_snapshot_defaults);
    RUN_TEST(test_ui_view_model_roundtrips_all_fields);
    RUN_TEST(test_ui_view_model_publishes_each_field_change);
    RUN_TEST(test_ui_view_model_reads_complete_concurrent_snapshots);
}
