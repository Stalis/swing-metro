#pragma once

#include "engine/fault_midi_message_sink.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

enum class FaultCommandStatus : std::uint8_t {
    Pending,
    Ready,
    Invalid,
};

struct FaultCommandResult {
    FaultCommandStatus status = FaultCommandStatus::Pending;
    FaultScenario scenario = FaultScenario::Baseline;
};

class FaultCommandParser {
  public:
    [[nodiscard]] auto push(char byte) noexcept -> FaultCommandResult {
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
        _buffer[_length] = '\0';
        const auto result =
            _overflow ? FaultCommandResult{FaultCommandStatus::Invalid, {}} : parse();
        reset();
        return result;
    }

  private:
    [[nodiscard]] auto parse() const noexcept -> FaultCommandResult {
        constexpr std::array<const char*, 4> commands = {
            "FAULT baseline", "FAULT retry_first_clock", "FAULT sustained_backpressure",
            "FAULT deterministic_disconnect"};
        for (std::size_t index = 0; index < commands.size(); ++index) {
            const char* expected = commands[index];
            const char* actual = _buffer.data();
            while (*expected != '\0' && *actual == *expected) {
                ++actual;
                ++expected;
            }
            if (*actual == '\0' && *expected == '\0') {
                return {FaultCommandStatus::Ready, static_cast<FaultScenario>(index)};
            }
        }
        return {FaultCommandStatus::Invalid, {}};
    }

    auto reset() noexcept -> void {
        _length = 0;
        _overflow = false;
    }

    std::array<char, 40> _buffer{};
    std::size_t _length = 0;
    bool _overflow = false;
};

} // namespace SwingMetro
