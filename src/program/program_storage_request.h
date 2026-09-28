#pragma once

#include "program/program_storage_controller.h"

#include <optional>

namespace SwingMetro {

class ProgramStorageRequest {
  public:
    explicit ProgramStorageRequest(ProgramStorageController* controller = nullptr) noexcept;

    auto enqueue(ProgramStorageCommand command) noexcept -> void;
    [[nodiscard]] auto process() -> std::optional<ProgramStoreStatus>;

  private:
    ProgramStorageController* _controller;
    std::optional<ProgramStorageCommand> _pendingCommand;
};

} // namespace SwingMetro
