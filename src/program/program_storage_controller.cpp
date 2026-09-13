#include "program/program_storage_controller.h"

namespace SwingMetro {

ProgramStorageController::ProgramStorageController(
    ProgramSlotStore& store, Counter<std::uint8_t>& tempo, Counter<std::uint8_t>& swing,
    Counter<std::uint8_t>& volume, Sequencer& sequencer, MidiClockSettings& midiClock) noexcept
    : _store(store), _tempo(tempo), _swing(swing), _volume(volume), _sequencer(sequencer),
      _midiClock(midiClock) {}

auto ProgramStorageController::perform(ProgramStorageAction action, std::uint8_t slot)
    -> ProgramStoreStatus {
    if (_sequencer.isRunning()) {
        return ProgramStoreStatus::TransportRunning;
    }
    if (action == ProgramStorageAction::Save) {
        const auto program = captureProgram(_tempo, _swing, _volume, _sequencer, _midiClock);
        const auto status = _store.save(slot, program);
        if (status != ProgramStoreStatus::Ok) {
            return status;
        }
        const auto currentStatus = _store.save(PROGRAM_CURRENT_SLOT, program);
        if (currentStatus == ProgramStoreStatus::Ok) {
            rememberCurrentProgram(program);
        }
        return currentStatus;
    }

    Program program;
    const auto status = _store.load(slot, program);
    if (status != ProgramStoreStatus::Ok) {
        return status;
    }
    if (!applyProgram(program, _tempo, _swing, _volume, _sequencer, _midiClock)) {
        return ProgramStoreStatus::InvalidProgram;
    }
    const auto currentStatus = _store.save(PROGRAM_CURRENT_SLOT, program);
    if (currentStatus == ProgramStoreStatus::Ok) {
        rememberCurrentProgram(program);
    }
    return currentStatus;
}

auto ProgramStorageController::restoreCurrentProgram() -> ProgramStoreStatus {
    Program program;
    const auto status = _store.load(PROGRAM_CURRENT_SLOT, program);
    if (status != ProgramStoreStatus::Ok) {
        program = Program{};
    }
    if (!applyProgram(program, _tempo, _swing, _volume, _sequencer, _midiClock)) {
        program = Program{};
        (void)applyProgram(program, _tempo, _swing, _volume, _sequencer, _midiClock);
    }
    rememberCurrentProgram(program);
    return status;
}

auto ProgramStorageController::syncCurrentProgramIfChanged() -> ProgramStoreStatus {
    if (_sequencer.isRunning()) {
        return ProgramStoreStatus::TransportRunning;
    }
    const auto program = captureProgram(_tempo, _swing, _volume, _sequencer, _midiClock);
    if (_hasCurrentProgramCrc && currentProgramCrc(program) == _currentProgramCrc) {
        return ProgramStoreStatus::Ok;
    }
    const auto status = _store.save(PROGRAM_CURRENT_SLOT, program);
    if (status == ProgramStoreStatus::Ok) {
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
