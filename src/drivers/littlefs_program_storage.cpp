#include "drivers/littlefs_program_storage.h"

#include <cstdio>

namespace SwingMetro {

auto LittleFsProgramStorage::mount() -> bool {
    if (mounted_) {
        return true;
    }
    if (!configured_) {
        if (!LittleFS.setConfig(config_)) {
            return false;
        }
        configured_ = true;
    }
    mounted_ = LittleFS.begin();
    return mounted_;
}

auto LittleFsProgramStorage::read(std::uint8_t slot, ProgramStorageCopy copy,
                                  ProgramStorageImage& image) -> ProgramStorageReadResult {
    if (!mounted_) {
        return ProgramStorageReadResult::Failed;
    }
    char path[24]{};
    if (!pathFor(slot, copy, path, sizeof(path))) {
        return ProgramStorageReadResult::Failed;
    }
    if (!LittleFS.exists(path)) {
        return ProgramStorageReadResult::Missing;
    }

    auto file = LittleFS.open(path, "r");
    if (!file) {
        return ProgramStorageReadResult::Failed;
    }
    const auto size = file.size();
    if (size > image.bytes.size()) {
        file.close();
        return ProgramStorageReadResult::Failed;
    }
    const auto bytesRead = file.read(image.bytes.data(), size);
    file.close();
    if (bytesRead < 0 || static_cast<std::size_t>(bytesRead) != size) {
        return ProgramStorageReadResult::Failed;
    }
    image.size = size;
    return ProgramStorageReadResult::Ok;
}

auto LittleFsProgramStorage::write(std::uint8_t slot, ProgramStorageCopy copy,
                                   const ProgramStorageImage& image) -> bool {
    if (!mounted_ || image.size > image.bytes.size()) {
        return false;
    }
    char path[24]{};
    if (!pathFor(slot, copy, path, sizeof(path))) {
        return false;
    }
    auto file = LittleFS.open(path, "w");
    if (!file) {
        return false;
    }
    const auto bytesWritten = file.write(image.bytes.data(), image.size);
    file.flush();
    file.close();
    return bytesWritten == image.size;
}

auto LittleFsProgramStorage::pathFor(std::uint8_t slot, ProgramStorageCopy copy, char* path,
                                     std::size_t pathSize) -> bool {
    if (!isValidProgramSlot(slot)) {
        return false;
    }
    const auto copyName = copy == ProgramStorageCopy::A ? 'a' : 'b';
    const auto written = std::snprintf(path, pathSize, "/sm-program-%02u-%c.bin", slot, copyName);
    return written > 0 && static_cast<std::size_t>(written) < pathSize;
}

} // namespace SwingMetro
