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
        return _store.save(slot, captureProgram(_tempo, _swing, _volume, _sequencer, _midiClock));
    }

    Program program;
    const auto status = _store.load(slot, program);
    if (status != ProgramStoreStatus::Ok) {
        return status;
    }
    return applyProgram(program, _tempo, _swing, _volume, _sequencer, _midiClock)
               ? ProgramStoreStatus::Ok
               : ProgramStoreStatus::InvalidProgram;
}

} // namespace SwingMetro
