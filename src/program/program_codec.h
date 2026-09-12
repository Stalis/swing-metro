#pragma once

#include "program/program.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

enum class ProgramCodecStatus : std::uint8_t {
    Ok,
    InvalidProgram,
    InvalidMagic,
    UnsupportedVersion,
    InvalidHeader,
    InvalidLength,
    InvalidCrc,
    MalformedTlv,
    InvalidField,
};

constexpr std::size_t PROGRAM_HEADER_SIZE = 16;
constexpr std::size_t PROGRAM_CURRENT_PAYLOAD_SIZE = 62;
constexpr std::size_t PROGRAM_MAX_PAYLOAD_SIZE = 255;
constexpr std::size_t PROGRAM_MAX_ENCODED_SIZE = PROGRAM_HEADER_SIZE + PROGRAM_MAX_PAYLOAD_SIZE;

struct EncodedProgram {
    std::array<std::uint8_t, PROGRAM_MAX_ENCODED_SIZE> bytes{};
    std::size_t size = 0;
};

[[nodiscard]] auto encodeProgram(const Program& program, std::uint32_t revision,
                                 EncodedProgram& encoded) -> ProgramCodecStatus;
[[nodiscard]] auto decodeProgram(const std::uint8_t* data, std::size_t size, Program& program,
                                 std::uint32_t& revision) -> ProgramCodecStatus;
[[nodiscard]] auto programCrc32(const std::uint8_t* data, std::size_t size) -> std::uint32_t;

} // namespace SwingMetro
