#pragma once

#include "program/program_storage.h"

#include <LittleFS.h>

namespace SwingMetro {

class LittleFsProgramStorage final : public ProgramStorageBackend {
  public:
    [[nodiscard]] auto mount() -> bool override;
    [[nodiscard]] auto read(std::uint8_t slot, ProgramStorageCopy copy, ProgramStorageImage& image)
        -> ProgramStorageReadResult override;
    [[nodiscard]] auto write(std::uint8_t slot, ProgramStorageCopy copy,
                             const ProgramStorageImage& image) -> bool override;

  private:
    [[nodiscard]] static auto pathFor(std::uint8_t slot, ProgramStorageCopy copy, char* path,
                                      std::size_t pathSize) -> bool;

    LittleFSConfig _config{false};
    bool _configured = false;
    bool _mounted = false;
};

} // namespace SwingMetro
