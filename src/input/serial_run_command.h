#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

enum class SerialRunCommandStatus : std::uint8_t {
    Pending,
    Ready,
    Invalid,
};

struct SerialRunCommand {
    std::uint32_t durationMs = 0;
    std::uint8_t bpm = 0;
    std::uint8_t swing = 0;
};

struct SerialRunCommandResult {
    SerialRunCommandStatus status = SerialRunCommandStatus::Pending;
    SerialRunCommand command{};
};

class SerialRunCommandParser {
  public:
    [[nodiscard]] auto push(char byte) noexcept -> SerialRunCommandResult {
        if (byte == '\r') {
            return {};
        }
        if (byte != '\n') {
            if (_length + 1 >= _buffer.size()) {
                _overflow = true;
            } else if (!_overflow) {
                _buffer[_length++] = byte;
            }
            return {};
        }

        SerialRunCommandResult result;
        if (!_overflow && _length != 0) {
            _buffer[_length] = '\0';
            result = parse();
        } else if (_overflow) {
            result.status = SerialRunCommandStatus::Invalid;
        }
        reset();
        return result;
    }

  private:
    static constexpr std::uint32_t MIN_DURATION_MS = 1'000;
    static constexpr std::uint32_t MAX_DURATION_MS = 3'600'000;
    static constexpr std::uint32_t MIN_BPM = 40;
    static constexpr std::uint32_t MAX_BPM = 240;
    static constexpr std::uint32_t MIN_SWING = 50;
    static constexpr std::uint32_t MAX_SWING = 75;

    [[nodiscard]] static auto consumeLiteral(const char*& cursor, const char* literal) noexcept
        -> bool {
        while (*literal != '\0') {
            if (*cursor++ != *literal++) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static auto consumeSpace(const char*& cursor) noexcept -> bool {
        if (*cursor != ' ') {
            return false;
        }
        ++cursor;
        return true;
    }

    [[nodiscard]] static auto consumeUnsigned(const char*& cursor, std::uint32_t& value) noexcept
        -> bool {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        value = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            const auto digit = static_cast<std::uint32_t>(*cursor - '0');
            if (value > (UINT32_MAX - digit) / 10U) {
                return false;
            }
            value = value * 10U + digit;
            ++cursor;
        }
        return true;
    }

    [[nodiscard]] auto parse() const noexcept -> SerialRunCommandResult {
        const char* cursor = _buffer.data();
        if (!consumeLiteral(cursor, "RUN") || !consumeSpace(cursor)) {
            return {SerialRunCommandStatus::Invalid, {}};
        }

        std::uint32_t durationMs = 0;
        std::uint32_t bpm = 0;
        std::uint32_t swing = 0;
        if (!consumeUnsigned(cursor, durationMs) || !consumeSpace(cursor) ||
            !consumeUnsigned(cursor, bpm) || !consumeSpace(cursor) ||
            !consumeUnsigned(cursor, swing) || *cursor != '\0' || durationMs < MIN_DURATION_MS ||
            durationMs > MAX_DURATION_MS || bpm < MIN_BPM || bpm > MAX_BPM || swing < MIN_SWING ||
            swing > MAX_SWING) {
            return {SerialRunCommandStatus::Invalid, {}};
        }
        return {SerialRunCommandStatus::Ready,
                {durationMs, static_cast<std::uint8_t>(bpm), static_cast<std::uint8_t>(swing)}};
    }

    auto reset() noexcept -> void {
        _length = 0;
        _overflow = false;
    }

    std::array<char, 48> _buffer{};
    std::size_t _length = 0;
    bool _overflow = false;
};

} // namespace SwingMetro
