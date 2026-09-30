#include "test_session.h"

#include "engine/session.h"
#include "program/program_runtime.h"

#include <array>
#include <unity.h>

namespace {

class Sink final : public SwingMetro::MidiMessageSink {
  public:
    auto send(const SwingMetro::MidiDeliveryAttempt& attempt) -> SwingMetro::SendResult override {
        messages[count++] = attempt.message;
        return SwingMetro::SendResult::Accepted;
    }

    std::array<SwingMetro::MidiMessage, 128> messages{};
    std::size_t count = 0;
};

void testSessionAppliesStoppedProgramToPlaybackAndMasterSettings() {
    Sink sink;
    SwingMetro::Session session{sink};
    SwingMetro::Program program;
    program.tempo = 180;
    program.swing = 75;
    program.midiClockMode = SwingMetro::MidiClockMode::Internal;
    program.steps[0] = {.enabled = true, .note = 60, .velocity = 90, .gate = 25};
    const auto id = SwingMetro::ProgramId::fromSlot(3);

    TEST_ASSERT_TRUE(session.applyProgram(program, id));
    TEST_ASSERT_EQUAL_UINT8(180, session.tempo().getValue());
    TEST_ASSERT_EQUAL_UINT8(75, session.playback().sequencer().getSwing());
    TEST_ASSERT_EQUAL_UINT8(60, *session.playback().sequencer().getStepMidiNote(0));
    TEST_ASSERT_TRUE(session.playback().selectedProgramId().has_value());
    TEST_ASSERT_EQUAL_UINT8(3, session.playback().selectedProgramId()->slot());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiClockMode::Internal),
                            static_cast<std::uint8_t>(session.midiClock().mode()));
}

void testSessionRejectsProgramSettingsWhileTransportRuns() {
    Sink sink;
    SwingMetro::Session session{sink};
    SwingMetro::Program program;
    program.tempo = 180;

    session.transport().toggle(0);
    TEST_ASSERT_FALSE(session.applyProgram(program, std::nullopt));
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_TEMPO, session.tempo().getValue());
}

void testSessionRoutesTransportThroughOwnedPlaybackSequencer() {
    Sink sink;
    SwingMetro::Session session{sink};
    SwingMetro::Program program;
    program.midiClockMode = SwingMetro::MidiClockMode::Internal;
    program.steps[0] = {.enabled = true, .note = 60, .velocity = 100, .gate = 100};
    TEST_ASSERT_TRUE(session.applyProgram(program, std::nullopt));
    SwingMetro::InternalTickStore<> ticks;

    session.transport().toggle(0);
    TEST_ASSERT_TRUE(ticks.publish({1'000, 20'000}));
    session.transport().process(1'000, ticks);

    TEST_ASSERT_EQUAL_UINT32(3, sink.count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiMessageType::Start),
                            static_cast<std::uint8_t>(sink.messages[0].type()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::MidiMessageType::Clock),
                            static_cast<std::uint8_t>(sink.messages[1].type()));
    TEST_ASSERT_TRUE(sink.messages[2].isNoteOn());
    TEST_ASSERT_EQUAL_UINT8(60, sink.messages[2].note());
}

void testSessionPlaybackMatchesDirectSequencerTrace() {
    for (const auto swing : {50, 75}) {
        for (const auto gate : {1, 25, 100}) {
            Sink sessionSink;
            Sink directSink;
            SwingMetro::Session session{sessionSink};
            Sequencer directSequencer;
            SwingMetro::MidiClockSettings directClock;
            Counter<std::uint8_t> directTempo{
                {.step = 1, .value = 120, .minValue = 40, .maxValue = 240}};
            Counter<std::uint8_t> directSwing{
                {.step = 1, .value = 50, .minValue = 50, .maxValue = 90}};
            Counter<std::uint8_t> directVolume{
                {.step = 1, .value = 100, .minValue = 0, .maxValue = 100}};
            SwingMetro::TransportController directTransport{directSequencer, directClock,
                                                            directSink};
            SwingMetro::Program program;
            program.tempo = 240;
            program.swing = static_cast<std::uint8_t>(swing);
            program.midiClockMode = SwingMetro::MidiClockMode::Internal;
            program.steps[0] = {.enabled = true,
                                .note = 60,
                                .velocity = 100,
                                .gate = static_cast<std::uint8_t>(gate)};
            program.steps[1] = {.enabled = true,
                                .note = 60,
                                .velocity = 100,
                                .gate = static_cast<std::uint8_t>(gate)};
            program.steps[15] = {.enabled = true,
                                 .note = 64,
                                 .velocity = 100,
                                 .gate = static_cast<std::uint8_t>(gate)};
            TEST_ASSERT_TRUE(session.applyProgram(program, std::nullopt));
            TEST_ASSERT_TRUE(SwingMetro::applyProgram(program, directTempo, directSwing,
                                                      directVolume, directSequencer, directClock));

            SwingMetro::InternalTickStore<> sessionTicks;
            SwingMetro::InternalTickStore<> directTicks;
            session.transport().toggle(0);
            directTransport.toggle(0);
            for (std::uint32_t tick = 1; tick <= 96; ++tick) {
                const auto nowUs = tick * 10'416U;
                TEST_ASSERT_TRUE(sessionTicks.publish({nowUs, 10'416}));
                TEST_ASSERT_TRUE(directTicks.publish({nowUs, 10'416}));
                session.transport().process(nowUs, sessionTicks);
                directTransport.process(nowUs, directTicks);
            }

            TEST_ASSERT_EQUAL_UINT32(directSink.count, sessionSink.count);
            for (std::size_t index = 0; index < directSink.count; ++index) {
                const auto& expected = directSink.messages[index];
                const auto& actual = sessionSink.messages[index];
                TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(expected.type()),
                                        static_cast<std::uint8_t>(actual.type()));
                TEST_ASSERT_EQUAL_UINT8(expected.channel(), actual.channel());
                TEST_ASSERT_EQUAL_UINT8(expected.note(), actual.note());
                TEST_ASSERT_EQUAL_UINT8(expected.velocity(), actual.velocity());
            }
        }
    }
}

} // namespace

void testSessionMain() {
    RUN_TEST(testSessionAppliesStoppedProgramToPlaybackAndMasterSettings);
    RUN_TEST(testSessionRejectsProgramSettingsWhileTransportRuns);
    RUN_TEST(testSessionRoutesTransportThroughOwnedPlaybackSequencer);
    RUN_TEST(testSessionPlaybackMatchesDirectSequencerTrace);
}
