#include "program/program_storage_controller.h"

namespace SwingMetro {

ProgramStorageController::ProgramStorageController(ProgramSlotStore& store, Session& session,
                                                   ProgramBank& bank, ProgramDraft& draft,
                                                   Counter<std::uint8_t>& swing,
                                                   Counter<std::uint8_t>& volume) noexcept
    : _store(store), _session(session), _bank(bank), _draft(draft), _swing(swing), _volume(volume) {
}

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
        const auto id = ProgramId::fromSlot(slot);
        const auto currentStatus = saveCurrentProgram(program, !id.has_value());
        if (currentStatus != ProgramStoreStatus::Ok || !id.has_value()) {
            return currentStatus;
        }
        const auto bankStatus = _bank.replace(*id, program);
        if (bankStatus != ProgramBankReplaceStatus::Ok) {
            return bankStatus == ProgramBankReplaceStatus::RevisionExhausted
                       ? ProgramStoreStatus::RevisionExhausted
                       : ProgramStoreStatus::InvalidProgram;
        }
        _session.playback().selectProgram(id);
        (void)_draft.load(program, id);
        return ProgramStoreStatus::Ok;
    }

    Program program;
    const auto status = _store.load(slot, program);
    if (status != ProgramStoreStatus::Ok) {
        return status;
    }
    const auto id = ProgramId::fromSlot(slot);
    if (!_session.applyProgram(program, id)) {
        return ProgramStoreStatus::InvalidProgram;
    }
    _swing.setValue(program.swing);
    _volume.setValue(program.volume);
    const auto currentStatus = saveCurrentProgram(program, !id.has_value());
    if (currentStatus != ProgramStoreStatus::Ok || !id.has_value()) {
        return currentStatus;
    }
    (void)_draft.load(program, id);
    return ProgramStoreStatus::Ok;
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
    const auto status = saveCurrentProgram(program, false);
    if (status == ProgramStoreStatus::Ok) {
        (void)_draft.load(program, std::nullopt);
    }
    return status;
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
    if (status == ProgramStoreStatus::Ok) {
        (void)_draft.load(program, std::nullopt);
    }
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

auto ProgramStorageController::saveCurrentProgram(const Program& program, bool updateDraft)
    -> ProgramStoreStatus {
    const auto status = _store.save(PROGRAM_CURRENT_SLOT, program);
    if (status == ProgramStoreStatus::Ok) {
        _session.playback().refreshAppliedProgram(program);
        if (updateDraft) {
            (void)_draft.load(program, _draft.sourceId());
        }
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
