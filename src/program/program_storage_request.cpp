#include "program/program_storage_request.h"

namespace SwingMetro {

ProgramStorageRequest::ProgramStorageRequest(ProgramStorageController* controller) noexcept
    : _controller(controller) {}

auto ProgramStorageRequest::enqueue(ProgramStorageCommand command) noexcept -> void {
    _pendingCommand = command;
}

auto ProgramStorageRequest::process() -> std::optional<ProgramStoreStatus> {
    if (!_pendingCommand.has_value()) {
        return std::nullopt;
    }

    const auto command = *_pendingCommand;
    _pendingCommand.reset();
    if (_controller == nullptr) {
        return ProgramStoreStatus::NotMounted;
    }
    if (command.operation == ProgramStorageOperation::Reset) {
        return _controller->resetCurrentProgram();
    }
    return _controller->perform(command.operation == ProgramStorageOperation::Save
                                    ? ProgramStorageAction::Save
                                    : ProgramStorageAction::Load,
                                command.slot);
}

} // namespace SwingMetro
