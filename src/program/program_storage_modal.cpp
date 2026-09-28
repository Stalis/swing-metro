#include "program/program_storage_modal.h"

#include <algorithm>

namespace SwingMetro {

auto ProgramStorageModal::open() noexcept -> void {
    _snapshot = {.state = ProgramStorageModalState::Action,
                 .selection = ProgramStorageMenuItem::Save,
                 .action = ProgramStorageAction::Save,
                 .slot = 0,
                 .status = ProgramStoreStatus::Ok,
                 .resetChoice = ProgramResetChoice::No};
    _closeRequested = false;
}

auto ProgramStorageModal::close() noexcept -> void {
    _snapshot.state = ProgramStorageModalState::Closed;
    _closeRequested = false;
}

auto ProgramStorageModal::selectAction(std::int8_t delta) noexcept -> void {
    if (_snapshot.state != ProgramStorageModalState::Action || delta == 0) {
        return;
    }

    const auto candidate = static_cast<int>(_snapshot.selection) + delta;
    _snapshot.selection = static_cast<ProgramStorageMenuItem>(
        std::clamp(candidate, static_cast<int>(ProgramStorageMenuItem::Save),
                   static_cast<int>(ProgramStorageMenuItem::ResetProgram)));
}

auto ProgramStorageModal::selectSlot(std::int8_t delta) noexcept -> void {
    if (_snapshot.state != ProgramStorageModalState::Slot || delta == 0) {
        return;
    }

    const auto current =
        _snapshot.slot == PROGRAM_STORAGE_CANCEL_SLOT ? -1 : static_cast<int>(_snapshot.slot);
    const auto selected =
        std::clamp(current + delta, -1, static_cast<int>(PROGRAM_USER_SLOT_COUNT - 1));
    _snapshot.slot =
        selected < 0 ? PROGRAM_STORAGE_CANCEL_SLOT : static_cast<std::uint8_t>(selected);
}

auto ProgramStorageModal::selectResetChoice(std::int8_t delta) noexcept -> void {
    if (_snapshot.state != ProgramStorageModalState::ResetConfirmation || delta == 0) {
        return;
    }

    const auto candidate = static_cast<int>(_snapshot.resetChoice) + delta;
    _snapshot.resetChoice = static_cast<ProgramResetChoice>(
        std::clamp(candidate, static_cast<int>(ProgramResetChoice::No),
                   static_cast<int>(ProgramResetChoice::Yes)));
}

auto ProgramStorageModal::confirmAction() noexcept -> bool {
    if (_snapshot.state != ProgramStorageModalState::Action) {
        return false;
    }

    if (_snapshot.selection == ProgramStorageMenuItem::Cancel) {
        _closeRequested = true;
        return false;
    }
    if (_snapshot.selection == ProgramStorageMenuItem::ResetProgram) {
        _snapshot.resetChoice = ProgramResetChoice::No;
        _snapshot.state = ProgramStorageModalState::ResetConfirmation;
        return true;
    }

    _snapshot.action = _snapshot.selection == ProgramStorageMenuItem::Save
                           ? ProgramStorageAction::Save
                           : ProgramStorageAction::Load;
    _snapshot.slot = 0;
    _snapshot.state = ProgramStorageModalState::Slot;
    return true;
}

auto ProgramStorageModal::confirmSlot() noexcept -> std::optional<ProgramStorageCommand> {
    if (_snapshot.state != ProgramStorageModalState::Slot) {
        return std::nullopt;
    }
    if (_snapshot.slot == PROGRAM_STORAGE_CANCEL_SLOT) {
        _closeRequested = true;
        return std::nullopt;
    }

    _snapshot.state = ProgramStorageModalState::Busy;
    return ProgramStorageCommand{.operation = _snapshot.action == ProgramStorageAction::Save
                                                  ? ProgramStorageOperation::Save
                                                  : ProgramStorageOperation::Load,
                                 .slot = _snapshot.slot};
}

auto ProgramStorageModal::confirmReset() noexcept -> std::optional<ProgramStorageCommand> {
    if (_snapshot.state != ProgramStorageModalState::ResetConfirmation) {
        return std::nullopt;
    }
    if (_snapshot.resetChoice == ProgramResetChoice::No) {
        _snapshot.state = ProgramStorageModalState::Action;
        return std::nullopt;
    }

    _snapshot.state = ProgramStorageModalState::Busy;
    return ProgramStorageCommand{.operation = ProgramStorageOperation::Reset};
}

auto ProgramStorageModal::complete(ProgramStoreStatus status) noexcept -> void {
    _snapshot.status = status;
    _snapshot.state = status == ProgramStoreStatus::Ok ? ProgramStorageModalState::Success
                                                       : ProgramStorageModalState::Error;
}

auto ProgramStorageModal::isOpen() const noexcept -> bool {
    return _snapshot.state != ProgramStorageModalState::Closed;
}

auto ProgramStorageModal::closeRequested() const noexcept -> bool { return _closeRequested; }

auto ProgramStorageModal::snapshot() const noexcept -> const ProgramStorageModalSnapshot& {
    return _snapshot;
}

} // namespace SwingMetro
