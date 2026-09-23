#include "test_program_codec.h"

#include "program/program_codec.h"

#include <cstring>
#include <unity.h>

namespace {

constexpr std::size_t HEADER_PREFIX_SIZE = 12;
constexpr std::size_t HEADER_CRC_OFFSET = 12;
constexpr std::size_t PAYLOAD_OFFSET = 16;
constexpr std::size_t SWING_TLV_OFFSET = 19;
constexpr std::size_t STEPS_VALUE_OFFSET = 30;
constexpr std::size_t GATE_TLV_OFFSET = 78;
constexpr std::size_t GATE_VALUE_OFFSET = 80;
constexpr std::size_t GATE_TLV_SIZE = 18;

auto sampleProgram() -> SwingMetro::Program {
    SwingMetro::Program program{
        .tempo = 180,
        .swing = 66,
        .volume = 75,
        .midiClockMode = SwingMetro::MidiClockMode::External,
    };
    program.steps[0] = {.enabled = true, .note = 48, .velocity = 64, .gate = 1};
    program.steps[1] = {.enabled = true, .note = 49, .velocity = 65, .gate = 25};
    program.steps[2] = {.enabled = true, .note = 50, .velocity = 66, .gate = 50};
    program.steps[3] = {.enabled = true, .note = 51, .velocity = 67, .gate = 75};
    program.steps[15] = {.enabled = true, .note = 127, .velocity = 1, .gate = 100};
    return program;
}

auto updateCrc(SwingMetro::EncodedProgram& encoded) -> void {
    std::array<std::uint8_t, SwingMetro::PROGRAM_MAX_ENCODED_SIZE - 4> protectedBytes{};
    std::memcpy(protectedBytes.data(), encoded.bytes.data(), HEADER_PREFIX_SIZE);
    std::memcpy(protectedBytes.data() + HEADER_PREFIX_SIZE, encoded.bytes.data() + PAYLOAD_OFFSET,
                encoded.size - PAYLOAD_OFFSET);
    const auto crc = SwingMetro::programCrc32(protectedBytes.data(), encoded.size - 4);
    for (std::size_t index = 0; index < 4; ++index) {
        encoded.bytes[HEADER_CRC_OFFSET + index] = static_cast<std::uint8_t>(crc >> (index * 8U));
    }
}

void testCodecRoundTripPreservesProgramAndRevision() {
    const auto source = sampleProgram();
    SwingMetro::EncodedProgram encoded;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(source, 42, encoded)));
    SwingMetro::Program decoded;
    std::uint32_t revision = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                encoded.bytes.data(), encoded.size, decoded, revision)));
    TEST_ASSERT_EQUAL_UINT32(42, revision);
    TEST_ASSERT_EQUAL_UINT8(source.tempo, decoded.tempo);
    TEST_ASSERT_EQUAL_UINT8(source.swing, decoded.swing);
    TEST_ASSERT_EQUAL_UINT8(source.volume, decoded.volume);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(source.midiClockMode),
                            static_cast<std::uint8_t>(decoded.midiClockMode));
    for (std::size_t index = 0; index < source.steps.size(); ++index) {
        TEST_ASSERT_EQUAL(source.steps[index].enabled, decoded.steps[index].enabled);
        TEST_ASSERT_EQUAL_UINT8(source.steps[index].note, decoded.steps[index].note);
        TEST_ASSERT_EQUAL_UINT8(source.steps[index].velocity, decoded.steps[index].velocity);
        TEST_ASSERT_EQUAL_UINT8(source.steps[index].gate, decoded.steps[index].gate);
    }
    TEST_ASSERT_EQUAL_UINT32(
        SwingMetro::PROGRAM_HEADER_SIZE + SwingMetro::PROGRAM_CURRENT_PAYLOAD_SIZE, encoded.size);
    TEST_ASSERT_EQUAL_UINT8(16, encoded.bytes[28]);
    TEST_ASSERT_EQUAL_UINT8(48, encoded.bytes[29]);
    TEST_ASSERT_EQUAL_UINT8(1, encoded.bytes[30]);
    TEST_ASSERT_EQUAL_UINT8(48, encoded.bytes[31]);
    TEST_ASSERT_EQUAL_UINT8(64, encoded.bytes[32]);
    TEST_ASSERT_EQUAL_UINT8(17, encoded.bytes[GATE_TLV_OFFSET]);
    TEST_ASSERT_EQUAL_UINT8(16, encoded.bytes[GATE_TLV_OFFSET + 1]);
}

void testCodecIsDeterministicAndHasBoundedSize() {
    const auto source = sampleProgram();
    SwingMetro::EncodedProgram first;
    SwingMetro::EncodedProgram second;

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
                            static_cast<std::uint8_t>(SwingMetro::encodeProgram(source, 7, first)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(source, 7, second)));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(SwingMetro::PROGRAM_MAX_ENCODED_SIZE, first.size);
    TEST_ASSERT_EQUAL_MEMORY(first.bytes.data(), second.bytes.data(), first.size);
}

void testCodecRejectsInvalidProgram() {
    auto invalid = sampleProgram();
    invalid.tempo = 39;
    SwingMetro::EncodedProgram encoded;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidProgram),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(invalid, 1, encoded)));
    invalid = sampleProgram();
    invalid.steps[0].gate = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidProgram),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(invalid, 1, encoded)));
    invalid = sampleProgram();
    invalid.swing = 91;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidProgram),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(invalid, 1, encoded)));
}

void testCodecClampsLegacySwingOnDecode() {
    SwingMetro::Program legacy = sampleProgram();
    legacy.swing = 90;
    SwingMetro::EncodedProgram encoded;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(legacy, 1, encoded)));
    encoded.bytes[SWING_TLV_OFFSET + 2] = 100;
    updateCrc(encoded);

    SwingMetro::Program decoded;
    std::uint32_t revision = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                encoded.bytes.data(), encoded.size, decoded, revision)));
    TEST_ASSERT_EQUAL_UINT8(90, decoded.swing);
    TEST_ASSERT_TRUE(SwingMetro::isValid(decoded));
}

void testCodecDefaultsMissingFieldsAndRewritesCurrentFormat() {
    SwingMetro::EncodedProgram encoded;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(sampleProgram(), 3, encoded)));
    constexpr std::size_t SWING_TLV_SIZE = 3;
    std::memmove(encoded.bytes.data() + SWING_TLV_OFFSET,
                 encoded.bytes.data() + SWING_TLV_OFFSET + SWING_TLV_SIZE,
                 encoded.size - SWING_TLV_OFFSET - SWING_TLV_SIZE);
    encoded.size -= SWING_TLV_SIZE;
    encoded.bytes[10] = static_cast<std::uint8_t>(encoded.size - PAYLOAD_OFFSET);
    encoded.bytes[11] = 0;
    updateCrc(encoded);

    SwingMetro::Program decoded;
    std::uint32_t revision = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                encoded.bytes.data(), encoded.size, decoded, revision)));
    TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_SWING, decoded.swing);
    SwingMetro::EncodedProgram rewritten;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(decoded, revision, rewritten)));
    TEST_ASSERT_EQUAL_UINT32(
        SwingMetro::PROGRAM_HEADER_SIZE + SwingMetro::PROGRAM_CURRENT_PAYLOAD_SIZE, rewritten.size);
}

void testCodecMigratesLegacyPayloadWithoutGate() {
    SwingMetro::EncodedProgram encoded;
    (void)SwingMetro::encodeProgram(sampleProgram(), 3, encoded);
    encoded.size -= GATE_TLV_SIZE;
    encoded.bytes[10] = static_cast<std::uint8_t>(encoded.size - PAYLOAD_OFFSET);
    encoded.bytes[11] = 0;
    updateCrc(encoded);

    SwingMetro::Program decoded;
    std::uint32_t revision = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                encoded.bytes.data(), encoded.size, decoded, revision)));
    for (const auto& step : decoded.steps) {
        TEST_ASSERT_EQUAL_UINT8(SwingMetro::PROGRAM_DEFAULT_GATE, step.gate);
    }
}

void testCodecRejectsMalformedAndInvalidGateTlvs() {
    SwingMetro::EncodedProgram encoded;
    (void)SwingMetro::encodeProgram(sampleProgram(), 1, encoded);
    SwingMetro::Program decoded;
    std::uint32_t revision = 0;

    for (const auto length : {0, 15, 17}) {
        auto broken = encoded;
        broken.bytes[GATE_TLV_OFFSET + 1] = length;
        updateCrc(broken);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::MalformedTlv),
            static_cast<std::uint8_t>(
                SwingMetro::decodeProgram(broken.bytes.data(), broken.size, decoded, revision)));
    }

    auto broken = encoded;
    broken.bytes[GATE_VALUE_OFFSET] = 0;
    updateCrc(broken);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidField),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));
    broken = encoded;
    broken.bytes[GATE_VALUE_OFFSET] = 101;
    updateCrc(broken);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidField),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));

    broken = encoded;
    std::memcpy(broken.bytes.data() + broken.size, broken.bytes.data() + GATE_TLV_OFFSET,
                GATE_TLV_SIZE);
    broken.size += GATE_TLV_SIZE;
    broken.bytes[10] = static_cast<std::uint8_t>(broken.size - PAYLOAD_OFFSET);
    updateCrc(broken);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::MalformedTlv),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));
}

void testCodecReadsGateRegardlessOfTlvOrder() {
    SwingMetro::EncodedProgram encoded;
    (void)SwingMetro::encodeProgram(sampleProgram(), 1, encoded);
    std::array<std::uint8_t, GATE_TLV_SIZE> gate{};
    std::memcpy(gate.data(), encoded.bytes.data() + GATE_TLV_OFFSET, gate.size());
    std::memmove(encoded.bytes.data() + PAYLOAD_OFFSET + gate.size(),
                 encoded.bytes.data() + PAYLOAD_OFFSET, GATE_TLV_OFFSET - PAYLOAD_OFFSET);
    std::memcpy(encoded.bytes.data() + PAYLOAD_OFFSET, gate.data(), gate.size());
    updateCrc(encoded);

    SwingMetro::Program decoded;
    std::uint32_t revision = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                encoded.bytes.data(), encoded.size, decoded, revision)));
    TEST_ASSERT_EQUAL_UINT8(1, decoded.steps[0].gate);
    TEST_ASSERT_EQUAL_UINT8(100, decoded.steps[15].gate);
}

void testCodecSkipsUnknownTlv() {
    SwingMetro::EncodedProgram encoded;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(sampleProgram(), 9, encoded)));
    encoded.bytes[encoded.size++] = 0x7F;
    encoded.bytes[encoded.size++] = 1;
    encoded.bytes[encoded.size++] = 0xA5;
    encoded.bytes[10] = static_cast<std::uint8_t>(encoded.size - PAYLOAD_OFFSET);
    updateCrc(encoded);

    SwingMetro::Program decoded;
    std::uint32_t revision = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                encoded.bytes.data(), encoded.size, decoded, revision)));
    TEST_ASSERT_EQUAL_UINT8(180, decoded.tempo);
    TEST_ASSERT_TRUE(decoded.steps[15].enabled);
}

void testCodecRejectsInvalidMagicVersionLengthAndCrc() {
    SwingMetro::EncodedProgram encoded;
    (void)SwingMetro::encodeProgram(sampleProgram(), 1, encoded);
    SwingMetro::Program decoded;
    std::uint32_t revision = 0;

    auto broken = encoded;
    broken.bytes[0] ^= 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidMagic),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));
    broken = encoded;
    broken.bytes[4] = 2;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::UnsupportedVersion),
        static_cast<std::uint8_t>(
            SwingMetro::decodeProgram(broken.bytes.data(), broken.size, decoded, revision)));
    broken = encoded;
    broken.bytes[10] = 1;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidLength),
        static_cast<std::uint8_t>(
            SwingMetro::decodeProgram(broken.bytes.data(), broken.size, decoded, revision)));
    broken = encoded;
    broken.bytes[PAYLOAD_OFFSET] ^= 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidCrc),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));
}

void testCodecRejectsMalformedOrInvalidKnownTlvs() {
    SwingMetro::EncodedProgram encoded;
    (void)SwingMetro::encodeProgram(sampleProgram(), 1, encoded);
    SwingMetro::Program decoded;
    std::uint32_t revision = 0;

    auto broken = encoded;
    broken.bytes[SWING_TLV_OFFSET + 1] = 2;
    updateCrc(broken);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::MalformedTlv),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));
    broken = encoded;
    broken.bytes[STEPS_VALUE_OFFSET] = 2;
    updateCrc(broken);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidField),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));
    broken = encoded;
    broken.bytes[27] = 3;
    updateCrc(broken);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidField),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));
    broken = encoded;
    broken.bytes[PAYLOAD_OFFSET + 1] = 255;
    updateCrc(broken);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::MalformedTlv),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));
}

void testCodecRejectsDuplicateKnownTlvAndTrailingBytes() {
    SwingMetro::EncodedProgram encoded;
    (void)SwingMetro::encodeProgram(sampleProgram(), 1, encoded);
    SwingMetro::Program decoded;
    std::uint32_t revision = 0;

    auto broken = encoded;
    broken.bytes[broken.size++] = 1;
    broken.bytes[broken.size++] = 1;
    broken.bytes[broken.size++] = 120;
    broken.bytes[10] = static_cast<std::uint8_t>(broken.size - PAYLOAD_OFFSET);
    updateCrc(broken);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::MalformedTlv),
                            static_cast<std::uint8_t>(SwingMetro::decodeProgram(
                                broken.bytes.data(), broken.size, decoded, revision)));

    broken = encoded;
    ++broken.size;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidLength),
        static_cast<std::uint8_t>(
            SwingMetro::decodeProgram(broken.bytes.data(), broken.size, decoded, revision)));
}

void testCrc32MatchesStandardVector() {
    constexpr char VALUE[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(
        0xCBF43926U, SwingMetro::programCrc32(reinterpret_cast<const std::uint8_t*>(VALUE), 9));
}

} // namespace

void testProgramCodecMain() {
    RUN_TEST(testCodecRoundTripPreservesProgramAndRevision);
    RUN_TEST(testCodecIsDeterministicAndHasBoundedSize);
    RUN_TEST(testCodecRejectsInvalidProgram);
    RUN_TEST(testCodecClampsLegacySwingOnDecode);
    RUN_TEST(testCodecDefaultsMissingFieldsAndRewritesCurrentFormat);
    RUN_TEST(testCodecMigratesLegacyPayloadWithoutGate);
    RUN_TEST(testCodecRejectsMalformedAndInvalidGateTlvs);
    RUN_TEST(testCodecReadsGateRegardlessOfTlvOrder);
    RUN_TEST(testCodecSkipsUnknownTlv);
    RUN_TEST(testCodecRejectsInvalidMagicVersionLengthAndCrc);
    RUN_TEST(testCodecRejectsMalformedOrInvalidKnownTlvs);
    RUN_TEST(testCodecRejectsDuplicateKnownTlvAndTrailingBytes);
    RUN_TEST(testCrc32MatchesStandardVector);
}
