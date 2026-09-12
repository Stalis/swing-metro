#include "program/program_slot_store.h"

#include <cstring>
#include <limits>

namespace {

struct CopyState {
    SwingMetro::ProgramStorageReadResult readResult = SwingMetro::ProgramStorageReadResult::Missing;
    SwingMetro::ProgramStorageImage image{};
    SwingMetro::Program program{};
    std::uint32_t revision = 0;
    bool valid = false;
};

enum class SelectionResult : std::uint8_t { Ok, Empty, Corrupt, Conflict, ReadFailed };

struct Selection {
    SelectionResult result = SelectionResult::Empty;
    SwingMetro::ProgramStorageCopy copy = SwingMetro::ProgramStorageCopy::A;
    const CopyState* state = nullptr;
};

[[nodiscard]] auto otherCopy(SwingMetro::ProgramStorageCopy copy)
    -> SwingMetro::ProgramStorageCopy {
    return copy == SwingMetro::ProgramStorageCopy::A ? SwingMetro::ProgramStorageCopy::B
                                                     : SwingMetro::ProgramStorageCopy::A;
}

[[nodiscard]] auto imagesMatch(const SwingMetro::ProgramStorageImage& first,
                               const SwingMetro::ProgramStorageImage& second) -> bool {
    return first.size == second.size &&
           std::memcmp(first.bytes.data(), second.bytes.data(), first.size) == 0;
}

auto readCopy(SwingMetro::ProgramStorageBackend& storage, std::uint8_t slot,
              SwingMetro::ProgramStorageCopy copy) -> CopyState {
    CopyState state;
    state.readResult = storage.read(slot, copy, state.image);
    if (state.readResult != SwingMetro::ProgramStorageReadResult::Ok ||
        state.image.size > state.image.bytes.size()) {
        return state;
    }
    state.valid =
        SwingMetro::decodeProgram(state.image.bytes.data(), state.image.size, state.program,
                                  state.revision) == SwingMetro::ProgramCodecStatus::Ok;
    return state;
}

[[nodiscard]] auto selectCopy(const CopyState& first, const CopyState& second) -> Selection {
    if (first.valid && second.valid) {
        if (first.revision > second.revision) {
            return {SelectionResult::Ok, SwingMetro::ProgramStorageCopy::A, &first};
        }
        if (second.revision > first.revision) {
            return {SelectionResult::Ok, SwingMetro::ProgramStorageCopy::B, &second};
        }
        if (imagesMatch(first.image, second.image)) {
            return {SelectionResult::Ok, SwingMetro::ProgramStorageCopy::A, &first};
        }
        return {SelectionResult::Conflict, SwingMetro::ProgramStorageCopy::A, nullptr};
    }
    if (first.valid) {
        return {SelectionResult::Ok, SwingMetro::ProgramStorageCopy::A, &first};
    }
    if (second.valid) {
        return {SelectionResult::Ok, SwingMetro::ProgramStorageCopy::B, &second};
    }
    if (first.readResult == SwingMetro::ProgramStorageReadResult::Failed ||
        second.readResult == SwingMetro::ProgramStorageReadResult::Failed) {
        return {SelectionResult::ReadFailed, SwingMetro::ProgramStorageCopy::A, nullptr};
    }
    if (first.readResult == SwingMetro::ProgramStorageReadResult::Missing &&
        second.readResult == SwingMetro::ProgramStorageReadResult::Missing) {
        return {SelectionResult::Empty, SwingMetro::ProgramStorageCopy::A, nullptr};
    }
    return {SelectionResult::Corrupt, SwingMetro::ProgramStorageCopy::A, nullptr};
}

[[nodiscard]] auto toStoreStatus(SelectionResult result) -> SwingMetro::ProgramStoreStatus {
    switch (result) {
    case SelectionResult::Ok:
        return SwingMetro::ProgramStoreStatus::Ok;
    case SelectionResult::Empty:
        return SwingMetro::ProgramStoreStatus::Empty;
    case SelectionResult::Corrupt:
    case SelectionResult::Conflict:
        return SwingMetro::ProgramStoreStatus::Corrupt;
    case SelectionResult::ReadFailed:
        return SwingMetro::ProgramStoreStatus::ReadFailed;
    }
    return SwingMetro::ProgramStoreStatus::Corrupt;
}

} // namespace

namespace SwingMetro {

ProgramSlotStore::ProgramSlotStore(ProgramStorageBackend& storage) noexcept : _storage(storage) {}

auto ProgramSlotStore::mount() -> ProgramStoreStatus {
    _mounted = _storage.mount();
    return _mounted ? ProgramStoreStatus::Ok : ProgramStoreStatus::MountFailed;
}

auto ProgramSlotStore::load(std::uint8_t slot, Program& program) -> ProgramStoreStatus {
    if (!_mounted) {
        return ProgramStoreStatus::NotMounted;
    }
    if (!isValidProgramSlot(slot)) {
        return ProgramStoreStatus::InvalidSlot;
    }
    const auto first = readCopy(_storage, slot, ProgramStorageCopy::A);
    const auto second = readCopy(_storage, slot, ProgramStorageCopy::B);
    const auto selection = selectCopy(first, second);
    if (selection.result != SelectionResult::Ok) {
        return toStoreStatus(selection.result);
    }
    program = selection.state->program;
    return ProgramStoreStatus::Ok;
}

auto ProgramSlotStore::save(std::uint8_t slot, const Program& program) -> ProgramStoreStatus {
    if (!_mounted) {
        return ProgramStoreStatus::NotMounted;
    }
    if (!isValidProgramSlot(slot)) {
        return ProgramStoreStatus::InvalidSlot;
    }
    if (!isValid(program)) {
        return ProgramStoreStatus::InvalidProgram;
    }

    const auto first = readCopy(_storage, slot, ProgramStorageCopy::A);
    const auto second = readCopy(_storage, slot, ProgramStorageCopy::B);
    if (first.readResult == ProgramStorageReadResult::Failed ||
        second.readResult == ProgramStorageReadResult::Failed) {
        return ProgramStoreStatus::ReadFailed;
    }

    const auto selection = selectCopy(first, second);
    ProgramStorageCopy target = ProgramStorageCopy::A;
    std::uint32_t revision = 1;
    if (selection.result == SelectionResult::Ok) {
        if (selection.state->revision == std::numeric_limits<std::uint32_t>::max()) {
            return ProgramStoreStatus::RevisionExhausted;
        }
        target = otherCopy(selection.copy);
        revision = selection.state->revision + 1;
    } else if (selection.result == SelectionResult::ReadFailed) {
        return ProgramStoreStatus::ReadFailed;
    } else if (selection.result == SelectionResult::Conflict) {
        return ProgramStoreStatus::Corrupt;
    }

    ProgramStorageImage encoded;
    if (encodeProgram(program, revision, encoded) != ProgramCodecStatus::Ok) {
        return ProgramStoreStatus::InvalidProgram;
    }
    if (!_storage.write(slot, target, encoded)) {
        return ProgramStoreStatus::WriteFailed;
    }

    ProgramStorageImage verified;
    if (_storage.read(slot, target, verified) != ProgramStorageReadResult::Ok ||
        !imagesMatch(encoded, verified)) {
        return ProgramStoreStatus::VerificationFailed;
    }
    return ProgramStoreStatus::Ok;
}

} // namespace SwingMetro
