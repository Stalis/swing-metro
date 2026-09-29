#include "note_lifecycle.h"

std::optional<MIDI_Note> NoteLifecycle::requestNoteOff() {
    if (_actualSoundingLaunch.has_value() &&
        (!_requestedNoteOff.has_value() ||
         _requestedNoteOff->launchId != _actualSoundingLaunch->launchId ||
         _requestedNoteOff->sessionGeneration != _actualSoundingLaunch->sessionGeneration)) {
        _requestedNoteOff = _actualSoundingLaunch;
        return _actualSoundingLaunch->note;
    }
    return std::nullopt;
}

void NoteLifecycle::notifyNoteOnAccepted(MIDI_Note note) {
    notifyNoteOnAccepted({note, 0, _sessionGeneration});
}

void NoteLifecycle::notifyNoteOnAccepted(const NoteLaunch& launch) {
    _actualSoundingLaunch = launch;
    if (!_projectedLaunch.has_value() || _projectedLaunch->launchId == launch.launchId) {
        _projectedLaunch = launch;
    }
    _requestedNoteOff.reset();
}

void NoteLifecycle::notifyNoteOnQueued(const NoteLaunch& launch) noexcept {
    _projectedLaunch = launch;
}

void NoteLifecycle::notifyNoteOnExpired(const NoteLaunch& launch) noexcept {
    if (_projectedLaunch.has_value() && _projectedLaunch->launchId == launch.launchId &&
        _projectedLaunch->sessionGeneration == launch.sessionGeneration) {
        _projectedLaunch.reset();
    }
}

void NoteLifecycle::notifyNoteOffAccepted(MIDI_Note note) {
    notifyNoteOffAccepted({note, 0, _sessionGeneration});
}

void NoteLifecycle::notifyNoteOffAccepted(const NoteLaunch& launch) {
    const auto matches = [&launch](const std::optional<NoteLaunch>& candidate) {
        return candidate.has_value() && candidate->note == launch.note &&
               (launch.launchId == 0 || candidate->launchId == launch.launchId) &&
               (launch.sessionGeneration == 0 || candidate->sessionGeneration == 0 ||
                candidate->sessionGeneration == launch.sessionGeneration);
    };
    if (matches(_actualSoundingLaunch)) {
        _actualSoundingLaunch.reset();
    }
    if (matches(_requestedNoteOff)) {
        _requestedNoteOff.reset();
    }
    if (matches(_projectedLaunch)) {
        _projectedLaunch.reset();
    }
}

void NoteLifecycle::cancelRequestedNoteOff() noexcept { _requestedNoteOff.reset(); }

void NoteLifecycle::abandonRemoteNoteState() noexcept {
    _actualSoundingLaunch.reset();
    _projectedLaunch.reset();
    _requestedNoteOff.reset();
    _remoteNoteState = RemoteNoteState::Unknown;
}

std::optional<MIDI_Note> NoteLifecycle::actualSoundingNote() const {
    return _actualSoundingLaunch.has_value() ? std::optional<MIDI_Note>{_actualSoundingLaunch->note}
                                             : std::nullopt;
}

std::optional<NoteLaunch> NoteLifecycle::actualSoundingLaunch() const noexcept {
    return _actualSoundingLaunch;
}

std::optional<NoteLaunch> NoteLifecycle::projectedOrActualLaunch() const noexcept {
    return _projectedLaunch.has_value() ? _projectedLaunch : _actualSoundingLaunch;
}

RemoteNoteState NoteLifecycle::remoteNoteState() const noexcept { return _remoteNoteState; }

void NoteLifecycle::beginCleanRemoteSession(std::uint32_t generation) noexcept {
    _actualSoundingLaunch.reset();
    _projectedLaunch.reset();
    _requestedNoteOff.reset();
    _sessionGeneration = generation;
    _remoteNoteState = RemoteNoteState::Clean;
}

void NoteLifecycle::setSessionGeneration(std::uint32_t generation) noexcept {
    _sessionGeneration = generation;
}

std::uint32_t NoteLifecycle::sessionGeneration() const noexcept { return _sessionGeneration; }
