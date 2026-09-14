#include "program/program_codec.h"

#include <cstring>

namespace {

constexpr std::uint8_t CONTAINER_VERSION = 1;
constexpr std::uint8_t TAG_TEMPO = 1;
constexpr std::uint8_t TAG_SWING = 2;
constexpr std::uint8_t TAG_VOLUME = 3;
constexpr std::uint8_t TAG_MIDI_CLOCK_MODE = 4;
constexpr std::uint8_t TAG_STEPS = 16;
[[maybe_unused]] constexpr std::uint8_t TAG_STEP_GATE = 17;
[[maybe_unused]] constexpr std::uint8_t TAG_STEP_REPEAT = 18;
constexpr std::size_t STEP_PAYLOAD_SIZE = SwingMetro::PROGRAM_STEP_COUNT * 3;
constexpr std::uint8_t MAGIC[] = {'S', 'M', 'P', 'R'};

static_assert(SwingMetro::PROGRAM_CURRENT_PAYLOAD_SIZE == 4 * 3 + 2 + STEP_PAYLOAD_SIZE);

auto writeU16(std::uint8_t* destination, std::uint16_t value) -> void {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
}

auto writeU32(std::uint8_t* destination, std::uint32_t value) -> void {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

[[nodiscard]] auto readU16(const std::uint8_t* source) -> std::uint16_t {
    return static_cast<std::uint16_t>(source[0]) | (static_cast<std::uint16_t>(source[1]) << 8U);
}

[[nodiscard]] auto readU32(const std::uint8_t* source) -> std::uint32_t {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] auto crc32Update(std::uint32_t crc, const std::uint8_t* data, std::size_t size)
    -> std::uint32_t {
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::size_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return crc;
}

auto appendTlv(std::uint8_t tag, const std::uint8_t* value, std::size_t valueSize,
               std::uint8_t* payload, std::size_t& payloadSize) -> void {
    payload[payloadSize++] = tag;
    payload[payloadSize++] = static_cast<std::uint8_t>(valueSize);
    std::memcpy(payload + payloadSize, value, valueSize);
    payloadSize += valueSize;
}

} // namespace

namespace SwingMetro {

auto programCrc32(const std::uint8_t* data, std::size_t size) -> std::uint32_t {
    return crc32Update(0xFFFFFFFFU, data, size) ^ 0xFFFFFFFFU;
}

auto encodeProgram(const Program& program, std::uint32_t revision, EncodedProgram& encoded)
    -> ProgramCodecStatus {
    if (!isValid(program)) {
        return ProgramCodecStatus::InvalidProgram;
    }

    auto* payload = encoded.bytes.data() + PROGRAM_HEADER_SIZE;
    std::size_t payloadSize = 0;
    appendTlv(TAG_TEMPO, &program.tempo, 1, payload, payloadSize);
    appendTlv(TAG_SWING, &program.swing, 1, payload, payloadSize);
    appendTlv(TAG_VOLUME, &program.volume, 1, payload, payloadSize);
    const auto midiClockMode = static_cast<std::uint8_t>(program.midiClockMode);
    appendTlv(TAG_MIDI_CLOCK_MODE, &midiClockMode, 1, payload, payloadSize);

    std::array<std::uint8_t, STEP_PAYLOAD_SIZE> steps{};
    for (std::size_t index = 0; index < PROGRAM_STEP_COUNT; ++index) {
        const auto offset = index * 3;
        steps[offset] = program.steps[index].enabled ? 1 : 0;
        steps[offset + 1] = program.steps[index].note;
        steps[offset + 2] = program.steps[index].velocity;
    }
    appendTlv(TAG_STEPS, steps.data(), steps.size(), payload, payloadSize);
    static_assert(PROGRAM_CURRENT_PAYLOAD_SIZE <= PROGRAM_MAX_PAYLOAD_SIZE);

    auto* header = encoded.bytes.data();
    std::memcpy(header, MAGIC, sizeof(MAGIC));
    header[4] = CONTAINER_VERSION;
    header[5] = static_cast<std::uint8_t>(PROGRAM_HEADER_SIZE);
    writeU32(header + 6, revision);
    writeU16(header + 10, static_cast<std::uint16_t>(payloadSize));
    const auto crc =
        crc32Update(crc32Update(0xFFFFFFFFU, header, 12), payload, payloadSize) ^ 0xFFFFFFFFU;
    writeU32(header + 12, crc);
    encoded.size = PROGRAM_HEADER_SIZE + payloadSize;
    return ProgramCodecStatus::Ok;
}

auto decodeProgram(const std::uint8_t* data, std::size_t size, Program& program,
                   std::uint32_t& revision) -> ProgramCodecStatus {
    if (data == nullptr || size < PROGRAM_HEADER_SIZE) {
        return ProgramCodecStatus::InvalidHeader;
    }
    if (std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0) {
        return ProgramCodecStatus::InvalidMagic;
    }
    if (data[4] != CONTAINER_VERSION) {
        return ProgramCodecStatus::UnsupportedVersion;
    }
    if (data[5] != PROGRAM_HEADER_SIZE) {
        return ProgramCodecStatus::InvalidHeader;
    }

    const auto payloadSize = readU16(data + 10);
    if (payloadSize > PROGRAM_MAX_PAYLOAD_SIZE || size != PROGRAM_HEADER_SIZE + payloadSize) {
        return ProgramCodecStatus::InvalidLength;
    }
    const auto expectedCrc = readU32(data + 12);
    const auto actualCrc =
        crc32Update(crc32Update(0xFFFFFFFFU, data, 12), data + PROGRAM_HEADER_SIZE, payloadSize) ^
        0xFFFFFFFFU;
    if (actualCrc != expectedCrc) {
        return ProgramCodecStatus::InvalidCrc;
    }

    Program decoded{};
    bool seenTempo = false;
    bool seenSwing = false;
    bool seenVolume = false;
    bool seenMidiClockMode = false;
    bool seenSteps = false;
    std::size_t offset = 0;
    const auto* payload = data + PROGRAM_HEADER_SIZE;
    while (offset < payloadSize) {
        if (payloadSize - offset < 2) {
            return ProgramCodecStatus::MalformedTlv;
        }
        const auto tag = payload[offset++];
        const auto length = payload[offset++];
        if (length > payloadSize - offset) {
            return ProgramCodecStatus::MalformedTlv;
        }
        const auto* value = payload + offset;
        offset += length;

        switch (tag) {
        case TAG_TEMPO:
            if (seenTempo || length != 1) {
                return ProgramCodecStatus::MalformedTlv;
            }
            decoded.tempo = value[0];
            seenTempo = true;
            break;
        case TAG_SWING:
            if (seenSwing || length != 1) {
                return ProgramCodecStatus::MalformedTlv;
            }
            decoded.swing = value[0];
            seenSwing = true;
            break;
        case TAG_VOLUME:
            if (seenVolume || length != 1) {
                return ProgramCodecStatus::MalformedTlv;
            }
            decoded.volume = value[0];
            seenVolume = true;
            break;
        case TAG_MIDI_CLOCK_MODE:
            if (seenMidiClockMode || length != 1) {
                return ProgramCodecStatus::MalformedTlv;
            }
            decoded.midiClockMode = static_cast<MidiClockMode>(value[0]);
            seenMidiClockMode = true;
            break;
        case TAG_STEPS:
            if (seenSteps || length != STEP_PAYLOAD_SIZE) {
                return ProgramCodecStatus::MalformedTlv;
            }
            for (std::size_t index = 0; index < PROGRAM_STEP_COUNT; ++index) {
                const auto stepOffset = index * 3;
                if (value[stepOffset] > 1) {
                    return ProgramCodecStatus::InvalidField;
                }
                decoded.steps[index] = {
                    .enabled = value[stepOffset] != 0,
                    .note = value[stepOffset + 1],
                    .velocity = value[stepOffset + 2],
                };
            }
            seenSteps = true;
            break;
        default:
            break;
        }
    }

    if (decoded.swing > PROGRAM_MAX_SWING && decoded.swing <= PROGRAM_LEGACY_MAX_SWING) {
        decoded.swing = PROGRAM_MAX_SWING;
    }
    if (!isValid(decoded)) {
        return ProgramCodecStatus::InvalidField;
    }
    program = decoded;
    revision = readU32(data + 6);
    return ProgramCodecStatus::Ok;
}

} // namespace SwingMetro
