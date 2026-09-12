#include "test_program.h"

#include "program/program_runtime.h"

#include <unity.h>

namespace {

auto makeCounter(std::uint8_t value, std::uint8_t min, std::uint8_t max) -> Counter<std::uint8_t> {
    return Counter<std::uint8_t>({
        .step = 1,
        .value = value,
        .minValue = min,
        .maxValue = max,
        .overflowBehavior = CounterOverflowBehavior::Clamp,
    });
}

void testProgramDefaultsAreValid() {
    const SwingMetro::Program program;

    TEST_ASSERT_TRUE(SwingMetro::isValid(program));
    TEST_ASSERT_EQUAL_UINT8(120, program.tempo);
    TEST_ASSERT_EQUAL_UINT8(50, program.swing);
    TEST_ASSERT_EQUAL_UINT8(100, program.volume);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(program.midiClockMode));
    for (const auto& step : program.steps) {
        TEST_ASSERT_FALSE(step.enabled);
        TEST_ASSERT_EQUAL_UINT8(36, step.note);
        TEST_ASSERT_EQUAL_UINT8(127, step.velocity);
    }
}

void testCaptureProgramReadsAllRuntimeOwners() {
    auto tempo = makeCounter(180, 40, 240);
    auto swing = makeCounter(75, 50, 100);
    auto volume = makeCounter(25, 0, 100);
    Sequencer sequencer;
    auto steps = sequencer.steps();
    steps[0] = {.isEnabled = true, .note = 48, .velocity = 64};
    steps[15] = {.isEnabled = true, .note = 127, .velocity = 1};
    sequencer.setSteps(steps);
    SwingMetro::MidiClockSettings midiClock;
    midiClock.apply(SwingMetro::MidiClockMode::External);

    const auto program = SwingMetro::captureProgram(tempo, swing, volume, sequencer, midiClock);

    TEST_ASSERT_EQUAL_UINT8(180, program.tempo);
    TEST_ASSERT_EQUAL_UINT8(75, program.swing);
    TEST_ASSERT_EQUAL_UINT8(25, program.volume);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::External),
                            static_cast<std::uint8_t>(program.midiClockMode));
    TEST_ASSERT_TRUE(program.steps[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(48, program.steps[0].note);
    TEST_ASSERT_EQUAL_UINT8(64, program.steps[0].velocity);
    TEST_ASSERT_TRUE(program.steps[15].enabled);
    TEST_ASSERT_EQUAL_UINT8(127, program.steps[15].note);
    TEST_ASSERT_EQUAL_UINT8(1, program.steps[15].velocity);
}

void testApplyProgramReplacesAllPersistedRuntimeState() {
    auto tempo = makeCounter(120, 40, 240);
    auto swing = makeCounter(50, 50, 100);
    auto volume = makeCounter(100, 0, 100);
    Sequencer sequencer;
    SwingMetro::MidiClockSettings midiClock;
    SwingMetro::Program program{
        .tempo = 200,
        .swing = 66,
        .volume = 33,
        .midiClockMode = SwingMetro::MidiClockMode::Internal,
    };
    program.steps[0] = {.enabled = true, .note = 60, .velocity = 96};
    program.steps[15] = {.enabled = true, .note = 127, .velocity = 1};

    TEST_ASSERT_TRUE(SwingMetro::applyProgram(program, tempo, swing, volume, sequencer, midiClock));
    TEST_ASSERT_EQUAL_UINT8(200, tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(66, swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(33, volume.getValue());
    TEST_ASSERT_EQUAL_UINT8(200, sequencer.getBpm());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Internal),
                            static_cast<std::uint8_t>(midiClock.mode()));
    const auto& steps = sequencer.steps();
    TEST_ASSERT_TRUE(steps[0].isEnabled);
    TEST_ASSERT_EQUAL_UINT8(60, steps[0].note);
    TEST_ASSERT_EQUAL_UINT8(96, steps[0].velocity);
    TEST_ASSERT_FALSE(steps[1].isEnabled);
    TEST_ASSERT_TRUE(steps[15].isEnabled);
    TEST_ASSERT_EQUAL_UINT8(127, steps[15].note);
    TEST_ASSERT_EQUAL_UINT8(1, steps[15].velocity);
}

void testApplyProgramRejectsInvalidProgramWithoutChanges() {
    auto tempo = makeCounter(120, 40, 240);
    auto swing = makeCounter(50, 50, 100);
    auto volume = makeCounter(100, 0, 100);
    Sequencer sequencer;
    SwingMetro::MidiClockSettings midiClock;
    SwingMetro::Program invalid;
    invalid.tempo = 39;

    TEST_ASSERT_FALSE(
        SwingMetro::applyProgram(invalid, tempo, swing, volume, sequencer, midiClock));
    TEST_ASSERT_EQUAL_UINT8(120, tempo.getValue());
    TEST_ASSERT_EQUAL_UINT8(50, swing.getValue());
    TEST_ASSERT_EQUAL_UINT8(100, volume.getValue());
    TEST_ASSERT_EQUAL_UINT8(120, sequencer.getBpm());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Off),
                            static_cast<std::uint8_t>(midiClock.mode()));
}

} // namespace

void testProgramMain() {
    RUN_TEST(testProgramDefaultsAreValid);
    RUN_TEST(testCaptureProgramReadsAllRuntimeOwners);
    RUN_TEST(testApplyProgramReplacesAllPersistedRuntimeState);
    RUN_TEST(testApplyProgramRejectsInvalidProgramWithoutChanges);
}
