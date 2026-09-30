#include "test_program_storage_request.h"

#include "program/program_storage_request.h"

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
    SwingMetro::ProgramStorageController controller{store, session, swing, volume};
    SwingMetro::ProgramStorageRequest request{&controller};

    State() {
        (void)store.mount();
        sequencer.stop();
    }
};

void testRequestReturnsEmptyWithoutCommand() {
    State state;
    TEST_ASSERT_FALSE(state.request.process().has_value());
}

void testRequestReturnsNotMountedForPendingCommand() {
    SwingMetro::ProgramStorageRequest request;
    request.enqueue({.operation = SwingMetro::ProgramStorageOperation::Save, .slot = 2});

    const auto status = request.process();
    TEST_ASSERT_TRUE(status.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::NotMounted),
                            static_cast<std::uint8_t>(*status));
}

void testRequestForwardsSaveLoadAndReset() {
    State state;
    state.volume.setValue(42);
    state.request.enqueue({.operation = SwingMetro::ProgramStorageOperation::Save, .slot = 3});
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(*state.request.process()));

    state.volume.setValue(99);
    state.request.enqueue({.operation = SwingMetro::ProgramStorageOperation::Load, .slot = 3});
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(*state.request.process()));
    TEST_ASSERT_EQUAL_UINT8(42, state.volume.getValue());

    state.request.enqueue({.operation = SwingMetro::ProgramStorageOperation::Reset});
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::Ok),
                            static_cast<std::uint8_t>(*state.request.process()));
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_VOLUME, state.volume.getValue());
}

void testRequestUsesControllerTransportGuard() {
    State state;
    state.session.transport().toggle(0);
    state.request.enqueue({.operation = SwingMetro::ProgramStorageOperation::Save, .slot = 0});
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(*state.request.process()));

    state.request.enqueue({.operation = SwingMetro::ProgramStorageOperation::Reset});
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramStoreStatus::TransportRunning),
        static_cast<std::uint8_t>(*state.request.process()));
    TEST_ASSERT_EQUAL_UINT32(0, state.storage.writes);
}

void testRequestConsumesCommandExactlyOnce() {
    State state;
    state.request.enqueue({.operation = SwingMetro::ProgramStorageOperation::Save, .slot = 1});
    TEST_ASSERT_TRUE(state.request.process().has_value());
    TEST_ASSERT_EQUAL_UINT32(2, state.storage.writes);
    TEST_ASSERT_FALSE(state.request.process().has_value());
    TEST_ASSERT_EQUAL_UINT32(2, state.storage.writes);
}

} // namespace

void testProgramStorageRequestMain() {
    RUN_TEST(testRequestReturnsEmptyWithoutCommand);
    RUN_TEST(testRequestReturnsNotMountedForPendingCommand);
    RUN_TEST(testRequestForwardsSaveLoadAndReset);
    RUN_TEST(testRequestUsesControllerTransportGuard);
    RUN_TEST(testRequestConsumesCommandExactlyOnce);
}
