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
        images[slot][copyIndex(copy)] = image;
        present[slot][copyIndex(copy)] = true;
        return true;
    }
};

struct State {
    Counter<std::uint8_t> tempo{{.step = 1, .value = 120, .minValue = 40, .maxValue = 240}};
    Counter<std::uint8_t> swing{{.step = 1, .value = 50, .minValue = 50, .maxValue = 100}};
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Save, 3)));
    state.volume.setValue(99);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(state.controller.perform(
                                SwingMetro::ProgramStorageAction::Load, 3)));
    TEST_ASSERT_EQUAL_UINT8(42, state.volume.getValue());
}

void testControllerRejectsRunningTransport() {
    State state;
    state.sequencer.toggleRunning(0);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(
            state.controller.perform(SwingMetro::ProgramStorageAction::Save, 0)));
}

} // namespace

void testProgramStorageControllerMain() {
    RUN_TEST(testControllerSavesAndLoadsSelectedSlot);
    RUN_TEST(testControllerRejectsRunningTransport);
}
