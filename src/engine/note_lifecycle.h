#pragma once

#include <cstdint>
#include <optional>

#include "midi_event_queue.h"

using MIDI_Note = uint8_t;

enum class RemoteNoteState : std::uint8_t { Clean, Unknown };

struct NoteLaunch {
    MIDI_Note note = 0;
    SwingMetro::MidiLaunchId launchId = 0;
    std::uint32_t sessionGeneration = 0;
};

class NoteLifecycle {
  public:
    [[nodiscard]] std::optional<MIDI_Note> requestNoteOff();
    void notifyNoteOnAccepted(MIDI_Note note);
    void notifyNoteOffAccepted(MIDI_Note note);
    void notifyNoteOnQueued(const NoteLaunch& launch) noexcept;
    void notifyNoteOnExpired(const NoteLaunch& launch) noexcept;
    void notifyNoteOnAccepted(const NoteLaunch& launch);
    void notifyNoteOffAccepted(const NoteLaunch& launch);
    void cancelRequestedNoteOff() noexcept;
    void abandonRemoteNoteState() noexcept;
    [[nodiscard]] std::optional<MIDI_Note> actualSoundingNote() const;
    [[nodiscard]] std::optional<NoteLaunch> actualSoundingLaunch() const noexcept;
    [[nodiscard]] std::optional<NoteLaunch> projectedOrActualLaunch() const noexcept;
    [[nodiscard]] RemoteNoteState remoteNoteState() const noexcept;
    void beginCleanRemoteSession(std::uint32_t generation = 0) noexcept;
    void setSessionGeneration(std::uint32_t generation) noexcept;
    [[nodiscard]] std::uint32_t sessionGeneration() const noexcept;

  private:
    std::optional<NoteLaunch> _actualSoundingLaunch;
    std::optional<NoteLaunch> _projectedLaunch;
    std::optional<NoteLaunch> _requestedNoteOff;
    RemoteNoteState _remoteNoteState = RemoteNoteState::Clean;
    std::uint32_t _sessionGeneration = 0;
};
