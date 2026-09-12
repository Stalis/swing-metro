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

auto sampleProgram() -> SwingMetro::Program {
    SwingMetro::Program program{
        .tempo = 180,
        .swing = 66,
        .volume = 75,
        .midiClockMode = SwingMetro::MidiClockMode::External,
    };
    program.steps[0] = {.enabled = true, .note = 48, .velocity = 64};
    program.steps[15] = {.enabled = true, .note = 127, .velocity = 1};
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

void test_codec_round_trip_preserves_program_and_revision() {
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
    }
    TEST_ASSERT_EQUAL_UINT32(
        SwingMetro::PROGRAM_HEADER_SIZE + SwingMetro::PROGRAM_CURRENT_PAYLOAD_SIZE, encoded.size);
}

void test_codec_is_deterministic_and_has_bounded_size() {
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

void test_codec_rejects_invalid_program() {
    auto invalid = sampleProgram();
    invalid.tempo = 39;
    SwingMetro::EncodedProgram encoded;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::InvalidProgram),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(invalid, 1, encoded)));
}

void test_codec_defaults_missing_fields_and_rewrites_current_format() {
    SwingMetro::EncodedProgram encoded;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(SwingMetro::ProgramCodecStatus::Ok),
        static_cast<std::uint8_t>(SwingMetro::encodeProgram(sampleProgram(), 3, encoded)));
    const std::size_t swingTlvSize = 3;
    std::memmove(encoded.bytes.data() + SWING_TLV_OFFSET,
                 encoded.bytes.data() + SWING_TLV_OFFSET + swingTlvSize,
                 encoded.size - SWING_TLV_OFFSET - swingTlvSize);
    encoded.size -= swingTlvSize;
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

void test_codec_skips_unknown_tlv() {
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

void test_codec_rejects_invalid_magic_version_length_and_crc() {
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

void test_codec_rejects_malformed_or_invalid_known_tlvs() {
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

void test_codec_rejects_duplicate_known_tlv_and_trailing_bytes() {
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

void test_crc32_matches_standard_vector() {
    constexpr char VALUE[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(
        0xCBF43926U, SwingMetro::programCrc32(reinterpret_cast<const std::uint8_t*>(VALUE), 9));
}

} // namespace

void test_program_codec_main() {
    RUN_TEST(test_codec_round_trip_preserves_program_and_revision);
    RUN_TEST(test_codec_is_deterministic_and_has_bounded_size);
    RUN_TEST(test_codec_rejects_invalid_program);
    RUN_TEST(test_codec_defaults_missing_fields_and_rewrites_current_format);
    RUN_TEST(test_codec_skips_unknown_tlv);
    RUN_TEST(test_codec_rejects_invalid_magic_version_length_and_crc);
    RUN_TEST(test_codec_rejects_malformed_or_invalid_known_tlvs);
    RUN_TEST(test_codec_rejects_duplicate_known_tlv_and_trailing_bytes);
    RUN_TEST(test_crc32_matches_standard_vector);
}
