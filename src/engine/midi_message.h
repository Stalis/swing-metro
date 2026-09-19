#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace SwingMetro {

enum class MidiMessageType : std::uint8_t {
    NoteOn,
    NoteOff,
    Start,
    Continue,
    Stop,
    Clock,
};

enum class MidiMessageClass : std::uint8_t {
    Clock,
    Transport,
    Note,
    Count,
};

static constexpr std::size_t MIDI_MESSAGE_CLASS_COUNT =
    static_cast<std::size_t>(MidiMessageClass::Count);

struct MidiMessageClassSummary {
    std::array<std::size_t, MIDI_MESSAGE_CLASS_COUNT> counts{};
};

class MidiMessage {
  public:
    constexpr MidiMessage() = default;

    [[nodiscard]] static constexpr auto noteOn(std::uint8_t channel, std::uint8_t note,
                                               std::uint8_t velocity) noexcept
        -> std::optional<MidiMessage> {
        return channel <= 15 && note <= 127 && velocity <= 127
                   ? std::optional<MidiMessage>{{MidiMessageType::NoteOn, channel, note, velocity}}
                   : std::nullopt;
    }

    [[nodiscard]] static constexpr auto noteOff(std::uint8_t channel, std::uint8_t note) noexcept
        -> std::optional<MidiMessage> {
        return channel <= 15 && note <= 127
                   ? std::optional<MidiMessage>{{MidiMessageType::NoteOff, channel, note, 0}}
                   : std::nullopt;
    }

    [[nodiscard]] static constexpr auto start() noexcept -> MidiMessage {
        return {MidiMessageType::Start, 0, 0, 0};
    }

    [[nodiscard]] static constexpr auto continuePlayback() noexcept -> MidiMessage {
        return {MidiMessageType::Continue, 0, 0, 0};
    }

    [[nodiscard]] static constexpr auto stop() noexcept -> MidiMessage {
        return {MidiMessageType::Stop, 0, 0, 0};
    }

    [[nodiscard]] static constexpr auto clock() noexcept -> MidiMessage {
        return {MidiMessageType::Clock, 0, 0, 0};
    }

    [[nodiscard]] constexpr auto type() const noexcept -> MidiMessageType { return _type; }
    [[nodiscard]] constexpr auto channel() const noexcept -> std::uint8_t { return _channel; }
    [[nodiscard]] constexpr auto note() const noexcept -> std::uint8_t { return _note; }
    [[nodiscard]] constexpr auto velocity() const noexcept -> std::uint8_t { return _velocity; }
    [[nodiscard]] constexpr auto isClock() const noexcept -> bool {
        return _type == MidiMessageType::Clock;
    }
    [[nodiscard]] constexpr auto isNoteOn() const noexcept -> bool {
        return _type == MidiMessageType::NoteOn && _velocity != 0;
    }
    [[nodiscard]] constexpr auto isNoteOffEquivalent() const noexcept -> bool {
        return _type == MidiMessageType::NoteOff ||
               (_type == MidiMessageType::NoteOn && _velocity == 0);
    }
    [[nodiscard]] constexpr auto priority() const noexcept -> std::uint8_t {
        return isClock() ? 0 : (isNoteOffEquivalent() ? 1 : (isNoteOn() ? 2 : 3));
    }

    [[nodiscard]] constexpr auto messageClass() const noexcept -> MidiMessageClass {
        return isClock() ? MidiMessageClass::Clock
                         : (isNoteOn() || isNoteOffEquivalent() ? MidiMessageClass::Note
                                                                : MidiMessageClass::Transport);
    }

  private:
    constexpr MidiMessage(MidiMessageType type, std::uint8_t channel, std::uint8_t note,
                          std::uint8_t velocity) noexcept
        : _type{type}, _channel{channel}, _note{note}, _velocity{velocity} {}

    MidiMessageType _type = MidiMessageType::Clock;
    std::uint8_t _channel = 0;
    std::uint8_t _note = 0;
    std::uint8_t _velocity = 0;
};

static_assert(sizeof(MidiMessage) == 4);
static_assert(std::is_trivially_copyable_v<MidiMessage>);

} // namespace SwingMetro
