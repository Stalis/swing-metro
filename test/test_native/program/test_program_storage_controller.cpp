#include "test_program_storage_controller.h"

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

    auto mount() -> bool override { return true; }
    auto read(std::uint8_t slot, SwingMetro::ProgramStorageCopy copy,
              SwingMetro::ProgramStorageImage& image)
        -> SwingMetro::ProgramStorageReadResult override {
        if (!present[slot][copyIndex(copy)]) {
            return SwingMetro::ProgramStorageReadResult::Missing;
        }
        image = images[slot][copyIndex(copy)];
        return SwingMetro::ProgramStorageReadResult::Ok;
    }
    auto write(std::uint8_t slot, SwingMetro::ProgramStorageCopy copy,
               const SwingMetro::ProgramStorageImage& image) -> bool override {
        ++writes;
        images[slot][copyIndex(copy)] = image;
        present[slot][copyIndex(copy)] = true;
        return true;
    }
};

struct State {
    Counter<std::uint8_t> tempo{{.step = 1, .value = 120, .minValue = 40, .maxValue = 240}};
    Counter<std::uint8_t> swing{
        {.step = 1, .value = 50, .minValue = 50, .maxValue = SwingMetro::SWING_MAX_VALUE}};
    Counter<std::uint8_t> volume{{.step = 1, .value = 100, .minValue = 0, .maxValue = 100}};
    Sequencer sequencer;
    SwingMetro::MidiClockSettings midiClock;
    FakeStorage storage;
    SwingMetro::ProgramSlotStore store{storage};
    SwingMetro::ProgramStorageController controller{store,  tempo,     swing,
                                                    volume, sequencer, midiClock};

    State() {
        (void)store.mount();
        sequencer.stop();
    }
};

void testControllerSavesAndLoadsSelectedSlot() {
    State state;
    state.volume.setValue(42);
    auto steps = state.sequencer.steps();
    steps[0].gate = 25;
    state.sequencer.setSteps(steps);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    state.volume.setValue(99);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Load, 3)));
    TEST_ASSERT_EQUAL_UINT8(42, state.volume.getValue());
    TEST_ASSERT_EQUAL_UINT8(25, *state.sequencer.getStepGate(0));
    TEST_ASSERT_EQUAL_UINT32(3, state.storage.writes);
}

void testControllerRejectsRunningTransport() {
    State state;
    state.sequencer.toggleRunning(0);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(
            state.controller.perform(SwingMetro::ProgramStorageAction::Save, 0)));
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
    TEST_ASSERT_TRUE(SwingMetro::applyProgram(changed, state.tempo, state.swing, state.volume,
                                              state.sequencer, state.midiClock));

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

    state.sequencer.toggleRunning(0);
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

    state.sequencer.toggleRunning(0);
    state.volume.setValue(43);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(state.controller.syncCurrentProgramIfChanged()));
    TEST_ASSERT_EQUAL_UINT32(1, state.storage.writes);
}

} // namespace

void testProgramStorageControllerMain() {
    RUN_TEST(testControllerSavesAndLoadsSelectedSlot);
    RUN_TEST(testControllerRejectsRunningTransport);
    RUN_TEST(testControllerRestoresCurrentProgramAndDefaults);
    RUN_TEST(testControllerResetsAndPersistsInitialProgram);
    RUN_TEST(testControllerAutosavesOnlyChangedStoppedProgram);
}
