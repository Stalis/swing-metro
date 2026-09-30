#include "test_program_storage_controller.h"

#include "program/program_runtime.h"
#include "program/program_storage_controller.h"

#include <array>
#include <unity.h>

namespace {

constexpr auto copyIndex(SwingMetro::ProgramStorageCopy copy) -> std::size_t {
    return copy == SwingMetro::ProgramStorageCopy::A ? 0 : 1;
}

struct FakeStorage final : SwingMetro::ProgramStorageBackend {
    std::array<std::array<SwingMetro::ProgramStorageImage, 2>, SwingMetro::PROGRAM_SLOT_COUNT>
        images{};
    std::array<std::array<bool, 2>, SwingMetro::PROGRAM_SLOT_COUNT> present{};
    std::size_t writes = 0;
    std::size_t reads = 0;
    bool failReads = false;
    bool failWrites = false;
    bool failCurrentWrites = false;

    auto mount() -> bool override { return true; }
    auto read(std::uint8_t slot, SwingMetro::ProgramStorageCopy copy,
              SwingMetro::ProgramStorageImage& image)
        -> SwingMetro::ProgramStorageReadResult override {
        ++reads;
        if (failReads) {
            return SwingMetro::ProgramStorageReadResult::Failed;
        }
        if (!present[slot][copyIndex(copy)]) {
            return SwingMetro::ProgramStorageReadResult::Missing;
        }
        image = images[slot][copyIndex(copy)];
        return SwingMetro::ProgramStorageReadResult::Ok;
    }
    auto write(std::uint8_t slot, SwingMetro::ProgramStorageCopy copy,
               const SwingMetro::ProgramStorageImage& image) -> bool override {
        ++writes;
        if (failWrites || (failCurrentWrites && slot == SwingMetro::PROGRAM_CURRENT_SLOT)) {
            return false;
        }
        images[slot][copyIndex(copy)] = image;
        present[slot][copyIndex(copy)] = true;
        return true;
    }
};

struct Sink final : SwingMetro::MidiMessageSink {
    auto send(const SwingMetro::MidiDeliveryAttempt&) -> SwingMetro::SendResult override {
        return SwingMetro::SendResult::Accepted;
    }
};

struct State {
    Sink sink;
    SwingMetro::Session session{sink};
    Counter<std::uint8_t>& tempo = session.tempo();
    Counter<std::uint8_t> swing{
        {.step = 1, .value = 50, .minValue = 50, .maxValue = SwingMetro::SWING_MAX_VALUE}};
    Counter<std::uint8_t> volume{{.step = 1, .value = 100, .minValue = 0, .maxValue = 100}};
    Sequencer& sequencer = session.playback().sequencer();
    SwingMetro::MidiClockSettings& midiClock = session.midiClock();
    FakeStorage storage;
    SwingMetro::ProgramSlotStore store{storage};
    SwingMetro::ProgramBank bank;
    SwingMetro::ProgramDraft draft;
    SwingMetro::ProgramStorageController controller{store, session, bank, draft, swing, volume};

    State() {
        (void)store.mount();
        sequencer.stop();
    }
};

void testControllerSavesAndLoadsSelectedSlot() {
    State state;
    state.volume.setValue(42);
    state.swing.setValue(80);
    state.sequencer.setSwing(state.swing.getValue());
    auto steps = state.sequencer.steps();
    steps[0].gate = 25;
    state.sequencer.setSteps(steps);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    state.volume.setValue(99);
    state.swing.setValue(50);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Load, 3)));
    TEST_ASSERT_EQUAL_UINT8(42, state.volume.getValue());
    TEST_ASSERT_EQUAL_UINT8(80, state.swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(25, *state.sequencer.getStepGate(0));
    TEST_ASSERT_EQUAL_UINT8(3, state.session.playback().selectedProgramId()->slot());
    TEST_ASSERT_EQUAL_UINT32(3, state.storage.writes);
}

void testControllerCommitsUserSlotToBankAndDraftOnlyAfterSave() {
    State state;
    state.volume.setValue(42);
    state.swing.setValue(80);
    state.sequencer.setSwing(state.swing.getValue());
    auto steps = state.sequencer.steps();
    steps[0].gate = 25;
    state.sequencer.setSteps(steps);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    const auto id = *SwingMetro::ProgramId::fromSlot(3);
    TEST_ASSERT_EQUAL_UINT8(42, state.bank.find(id)->program.volume);
    TEST_ASSERT_EQUAL_UINT32(1, state.bank.find(id)->revision);
    TEST_ASSERT_TRUE(state.draft.sourceId().has_value());
    TEST_ASSERT_EQUAL_UINT8(3, state.draft.sourceId()->slot());
    TEST_ASSERT_EQUAL_UINT8(25, state.draft.program().steps[0].gate);

    state.volume.setValue(99);
    state.sequencer.setSwing(50);
    state.storage.failWrites = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::WriteFailed),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    TEST_ASSERT_EQUAL_UINT8(42, state.bank.find(id)->program.volume);
    TEST_ASSERT_EQUAL_UINT32(1, state.bank.find(id)->revision);
    TEST_ASSERT_EQUAL_UINT8(42, state.draft.program().volume);
    TEST_ASSERT_EQUAL_UINT8(80, state.draft.program().swing);
    TEST_ASSERT_EQUAL_UINT8(3, state.draft.sourceId()->slot());
}

void testControllerKeepsRamSnapshotIfCurrentAutosaveFails() {
    State state;
    state.volume.setValue(42);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    const auto id = *SwingMetro::ProgramId::fromSlot(3);
    state.volume.setValue(99);
    state.storage.failCurrentWrites = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::WriteFailed),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    TEST_ASSERT_EQUAL_UINT32(1, state.bank.find(id)->revision);
    TEST_ASSERT_EQUAL_UINT8(42, state.bank.find(id)->program.volume);
    TEST_ASSERT_EQUAL_UINT8(42, state.draft.program().volume);
    TEST_ASSERT_EQUAL_UINT8(3, state.session.playback().selectedProgramId()->slot());
}

void testControllerLoadsUserSlotIntoStoppedSessionBankAndDraft() {
    State state;
    state.tempo.setValue(180);
    state.sequencer.setBpm(180);
    state.midiClock.apply(SwingMetro::MidiClockMode::Internal);
    state.volume.setValue(42);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));

    state.tempo.setValue(120);
    state.sequencer.setBpm(120);
    state.midiClock.apply(SwingMetro::MidiClockMode::Off);
    state.volume.setValue(99);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Load, 3)));
    const auto id = *SwingMetro::ProgramId::fromSlot(3);
    TEST_ASSERT_EQUAL_UINT8(180, state.tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Internal),
                            static_cast<std::uint8_t>(state.midiClock.mode()));
    TEST_ASSERT_EQUAL_UINT8(42, state.bank.find(id)->program.volume);
    TEST_ASSERT_EQUAL_UINT32(1, state.bank.find(id)->revision);
    TEST_ASSERT_TRUE(state.draft.sourceId().has_value());
    TEST_ASSERT_EQUAL_UINT8(3, state.draft.sourceId()->slot());

    const auto before = state.draft.program();
    state.storage.failReads = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::ReadFailed),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Load, 4)));
    TEST_ASSERT_EQUAL_UINT8(42, state.bank.find(id)->program.volume);
    TEST_ASSERT_EQUAL_UINT8(before.tempo, state.draft.program().tempo);
    TEST_ASSERT_EQUAL_UINT8(before.volume, state.draft.program().volume);
    TEST_ASSERT_EQUAL_UINT8(3, state.draft.sourceId()->slot());
}

void testControllerAutosaveRefreshesDraftWithoutChangingBankOrSource() {
    State state;
    state.volume.setValue(42);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    const auto id = *SwingMetro::ProgramId::fromSlot(3);

    state.swing.setValue(80);
    state.sequencer.setSwing(state.swing.getValue());
    auto steps = state.sequencer.steps();
    steps[0].gate = 25;
    state.sequencer.setSteps(steps);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
        static_cast<std::uint8_t>(state.controller.syncCurrentProgramIfChanged()));
    TEST_ASSERT_EQUAL_UINT8(80, state.draft.program().swing);
    TEST_ASSERT_EQUAL_UINT8(25, state.draft.program().steps[0].gate);
    TEST_ASSERT_EQUAL_UINT8(42, state.bank.find(id)->program.volume);
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_SWING, state.bank.find(id)->program.swing);
    TEST_ASSERT_EQUAL_UINT8(3, state.draft.sourceId()->slot());

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
        static_cast<std::uint8_t>(state.controller.perform(SwingMetro::ProgramStorageAction::Save,
                                                           SwingMetro::PROGRAM_CURRENT_SLOT)));
    TEST_ASSERT_TRUE(state.session.playback().selectedProgramId().has_value());
    TEST_ASSERT_EQUAL_UINT8(3, state.session.playback().selectedProgramId()->slot());
    TEST_ASSERT_EQUAL_UINT32(1, state.bank.find(id)->revision);
    TEST_ASSERT_EQUAL_UINT8(3, state.draft.sourceId()->slot());
}

void testControllerReadsLiveDraftDuringPlaybackWithoutStorageAccess() {
    State state;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    const auto id = *SwingMetro::ProgramId::fromSlot(3);
    const auto writesBefore = state.storage.writes;
    const auto readsBefore = state.storage.reads;

    state.session.transport().toggle(0);
    state.sequencer.toggleStep(0);
    state.swing.setValue(80);
    state.sequencer.setSwing(80);
    const auto draft = state.controller.currentDraftSnapshot();

    TEST_ASSERT_TRUE(draft.sourceId().has_value());
    TEST_ASSERT_EQUAL_UINT8(3, draft.sourceId()->slot());
    TEST_ASSERT_TRUE(draft.program().steps[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(80, draft.program().swing);
    TEST_ASSERT_FALSE(state.bank.find(id)->program.steps[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(50, state.bank.find(id)->program.swing);
    TEST_ASSERT_EQUAL_UINT32(writesBefore, state.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(readsBefore, state.storage.reads);
}

void testControllerRejectsRunningTransport() {
    State state;
    state.session.transport().toggle(0);
    const auto readsBefore = state.storage.reads;
    const auto writesBefore = state.storage.writes;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(
            state.controller.perform(SwingMetro::ProgramStorageAction::Save, 0)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(
            state.controller.perform(SwingMetro::ProgramStorageAction::Load, 0)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(state.controller.restoreCurrentProgram()));
    TEST_ASSERT_EQUAL_UINT32(readsBefore, state.storage.reads);
    TEST_ASSERT_EQUAL_UINT32(writesBefore, state.storage.writes);
}

void testControllerRestoresCurrentProgramAndDefaults() {
    State state;
    state.volume.setValue(42);
    auto steps = state.sequencer.steps();
    steps[0].gate = 75;
    state.sequencer.setSteps(steps);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    state.volume.setValue(99);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.restoreCurrentProgram()));
    TEST_ASSERT_EQUAL_UINT8(42, state.volume.getValue());
    TEST_ASSERT_EQUAL_UINT8(75, *state.sequencer.getStepGate(0));

    State defaults;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Empty),
                            static_cast<std::uint8_t>(defaults.controller.restoreCurrentProgram()));
    TEST_ASSERT_EQUAL_UINT8(100, defaults.volume.getValue());
    TEST_ASSERT_EQUAL_UINT32(0, defaults.storage.writes);
}

void testControllerResetsAndPersistsInitialProgram() {
    State state;
    SwingMetro::Program changed;
    changed.tempo = 200;
    changed.swing = 80;
    changed.volume = 42;
    changed.midiClockMode = SwingMetro::MidiClockMode::Internal;
    changed.steps[0] = {.enabled = true, .note = 72, .velocity = 64, .gate = 25};
    TEST_ASSERT_TRUE(state.session.applyProgram(changed, std::nullopt));
    state.volume.setValue(changed.volume);
    TEST_ASSERT_TRUE(state.draft.load(changed, *SwingMetro::ProgramId::fromSlot(3)));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.resetCurrentProgram()));
    const auto reset = SwingMetro::captureProgram(state.tempo, state.swing, state.volume,
                                                  state.sequencer, state.midiClock);
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_TEMPO, reset.tempo);
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_SWING, reset.swing);
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_VOLUME, reset.volume);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(reset.midiClockMode));
    TEST_ASSERT_FALSE(reset.steps[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_NOTE, reset.steps[0].note);
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_VELOCITY, reset.steps[0].velocity);
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_GATE, reset.steps[0].gate);
    TEST_ASSERT_EQUAL_UINT32(1, state.storage.writes);
    TEST_ASSERT_FALSE(state.draft.sourceId().has_value());

    state.session.transport().toggle(0);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(state.controller.resetCurrentProgram()));
    TEST_ASSERT_EQUAL_UINT32(1, state.storage.writes);
}

void testControllerAutosavesOnlyChangedStoppedProgram() {
    State state;
    (void)state.controller.restoreCurrentProgram();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
        static_cast<std::uint8_t>(state.controller.syncCurrentProgramIfChanged()));
    TEST_ASSERT_EQUAL_UINT32(0, state.storage.writes);

    state.volume.setValue(42);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
        static_cast<std::uint8_t>(state.controller.syncCurrentProgramIfChanged()));
    TEST_ASSERT_EQUAL_UINT32(1, state.storage.writes);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
        static_cast<std::uint8_t>(state.controller.syncCurrentProgramIfChanged()));
    TEST_ASSERT_EQUAL_UINT32(1, state.storage.writes);

    state.session.transport().toggle(0);
    state.volume.setValue(43);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(state.controller.syncCurrentProgramIfChanged()));
    TEST_ASSERT_EQUAL_UINT32(1, state.storage.writes);
}

} // namespace

void testProgramStorageControllerMain() {
    RUN_TEST(testControllerSavesAndLoadsSelectedSlot);
    RUN_TEST(testControllerCommitsUserSlotToBankAndDraftOnlyAfterSave);
    RUN_TEST(testControllerKeepsRamSnapshotIfCurrentAutosaveFails);
    RUN_TEST(testControllerLoadsUserSlotIntoStoppedSessionBankAndDraft);
    RUN_TEST(testControllerAutosaveRefreshesDraftWithoutChangingBankOrSource);
    RUN_TEST(testControllerReadsLiveDraftDuringPlaybackWithoutStorageAccess);
    RUN_TEST(testControllerRejectsRunningTransport);
    RUN_TEST(testControllerRestoresCurrentProgramAndDefaults);
    RUN_TEST(testControllerResetsAndPersistsInitialProgram);
    RUN_TEST(testControllerAutosavesOnlyChangedStoppedProgram);
}
