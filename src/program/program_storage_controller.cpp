#include "program/program_storage_controller.h"

namespace SwingMetro {

ProgramStorageController::ProgramStorageController(ProgramSlotStore& store, Session& session,
                                                   Counter<std::uint8_t>& swing,
                                                   Counter<std::uint8_t>& volume) noexcept
    : _store(store), _session(session), _swing(swing), _volume(volume) {}

auto ProgramStorageController::perform(ProgramStorageAction action, std::uint8_t slot)
    -> ProgramStoreStatus {
    if (_session.isRunning()) {
        return ProgramStoreStatus::TransportRunning;
    }
    if (action == ProgramStorageAction::Save) {
        const auto program = captureProgram(_session.tempo(), _swing, _volume,
                                            _session.playback().sequencer(), _session.midiClock());
        const auto status = _store.save(slot, program);
        if (status != ProgramStoreStatus::Ok) {
            return status;
        }
        _session.playback().selectProgram(ProgramId::fromSlot(slot));
        return saveCurrentProgram(program);
    }

    Program program;
    const auto status = _store.load(slot, program);
    if (status != ProgramStoreStatus::Ok) {
        return status;
    }
    if (!_session.applyProgram(program, ProgramId::fromSlot(slot))) {
        return ProgramStoreStatus::InvalidProgram;
    }
    _swing.setValue(program.swing);
    _volume.setValue(program.volume);
    return saveCurrentProgram(program);
}

auto ProgramStorageController::resetCurrentProgram() -> ProgramStoreStatus {
    if (_session.isRunning()) {
        return ProgramStoreStatus::TransportRunning;
    }
    const Program program{};
    if (!_session.applyProgram(program, std::nullopt)) {
        return ProgramStoreStatus::InvalidProgram;
    }
    _swing.setValue(program.swing);
    _volume.setValue(program.volume);
    return saveCurrentProgram(program);
}

auto ProgramStorageController::restoreCurrentProgram() -> ProgramStoreStatus {
    if (_session.isRunning()) {
        return ProgramStoreStatus::TransportRunning;
    }
    Program program;
    const auto status = _store.load(PROGRAM_CURRENT_SLOT, program);
    if (status != ProgramStoreStatus::Ok) {
        program = Program{};
    }
    if (!_session.applyProgram(program, std::nullopt)) {
        program = Program{};
        (void)_session.applyProgram(program, std::nullopt);
    }
    _swing.setValue(program.swing);
    _volume.setValue(program.volume);
    rememberCurrentProgram(program);
    return status;
}

auto ProgramStorageController::syncCurrentProgramIfChanged() -> ProgramStoreStatus {
    if (_session.isRunning()) {
        return ProgramStoreStatus::TransportRunning;
    }
    const auto program = captureProgram(_session.tempo(), _swing, _volume,
                                        _session.playback().sequencer(), _session.midiClock());
    if (_hasCurrentProgramCrc && currentProgramCrc(program) == _currentProgramCrc) {
        return ProgramStoreStatus::Ok;
    }
    return saveCurrentProgram(program);
}

auto ProgramStorageController::saveCurrentProgram(const Program& program) -> ProgramStoreStatus {
    const auto status = _store.save(PROGRAM_CURRENT_SLOT, program);
    if (status == ProgramStoreStatus::Ok) {
        _session.playback().refreshAppliedProgram(program);
        rememberCurrentProgram(program);
    }
    return status;
}

auto ProgramStorageController::currentProgramCrc(const Program& program) const -> std::uint32_t {
    EncodedProgram encoded;
    if (encodeProgram(program, 0, encoded) != ProgramCodecStatus::Ok) {
        return 0;
    }
    return programCrc32(encoded.bytes.data() + PROGRAM_HEADER_SIZE,
                        encoded.size - PROGRAM_HEADER_SIZE);
}

auto ProgramStorageController::rememberCurrentProgram(const Program& program) -> void {
    _currentProgramCrc = currentProgramCrc(program);
    _hasCurrentProgramCrc = true;
}

} // namespace SwingMetro
