#pragma once

#include "program/program_storage.h"

#include <cstdint>
#include <optional>

namespace SwingMetro {

class ProgramId {
  public:
    [[nodiscard]] static constexpr auto fromSlot(std::uint8_t slot) -> std::optional<ProgramId> {
        if (slot >= PROGRAM_USER_SLOT_COUNT) {
            return std::nullopt;
        }
        return ProgramId(slot);
    }

    [[nodiscard]] constexpr auto slot() const -> std::uint8_t { return _slot; }

  private:
    explicit constexpr ProgramId(std::uint8_t slot) : _slot(slot) {}

    std::uint8_t _slot;
};

} // namespace SwingMetro
