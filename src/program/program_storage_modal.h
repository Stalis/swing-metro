#pragma once

#include "program/program_slot_store.h"
#include "program/program_storage.h"

#include <cstdint>
#include <optional>

namespace SwingMetro {

enum class ProgramStorageAction : std::uint8_t { Save, Load };
enum class ProgramStorageMenuItem : std::uint8_t { Save, Load, Cancel, ResetProgram };
enum class ProgramResetChoice : std::uint8_t { No, Yes };
enum class ProgramStorageModalState : std::uint8_t {
    Closed,
    Action,
    Slot,
    Busy,
    Success,
    Error,
    ResetConfirmation,
};
constexpr std::uint8_t PROGRAM_STORAGE_CANCEL_SLOT = UINT8_MAX;

enum class ProgramStorageOperation : std::uint8_t { Save, Load, Reset };

struct ProgramStorageCommand {
    ProgramStorageOperation operation = ProgramStorageOperation::Save;
    std::uint8_t slot = 0;
};

struct ProgramStorageModalSnapshot {
    ProgramStorageModalState state = ProgramStorageModalState::Closed;
    ProgramStorageMenuItem selection = ProgramStorageMenuItem::Save;
    ProgramStorageAction action = ProgramStorageAction::Save;
    std::uint8_t slot = 0;
    ProgramStoreStatus status = ProgramStoreStatus::Ok;
    ProgramResetChoice resetChoice = ProgramResetChoice::No;
};

class ProgramStorageModal {
  public:
    auto open() noexcept -> void;
    auto close() noexcept -> void;
    auto selectAction(std::int8_t delta) noexcept -> void;
    auto selectSlot(std::int8_t delta) noexcept -> void;
    auto selectResetChoice(std::int8_t delta) noexcept -> void;
    [[nodiscard]] auto confirmAction() noexcept -> bool;
    [[nodiscard]] auto confirmSlot() noexcept -> std::optional<ProgramStorageCommand>;
    [[nodiscard]] auto confirmReset() noexcept -> std::optional<ProgramStorageCommand>;
    auto complete(ProgramStoreStatus status) noexcept -> void;

    [[nodiscard]] auto isOpen() const noexcept -> bool;
    [[nodiscard]] auto closeRequested() const noexcept -> bool;
    [[nodiscard]] auto snapshot() const noexcept -> const ProgramStorageModalSnapshot&;

  private:
    ProgramStorageModalSnapshot _snapshot;
    bool _closeRequested = false;
};

} // namespace SwingMetro
