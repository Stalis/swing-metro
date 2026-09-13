#include "drivers/littlefs_program_storage.h"
#include "program/program_migration.h"

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr std::uint8_t PROTOCOL_VERSION = 1;
constexpr std::uint8_t TYPE_HELLO = 1;
constexpr std::uint8_t TYPE_EXPORT_CONFIRM = 2;
constexpr std::uint8_t TYPE_EXPORT_DATA = 3;
constexpr std::uint8_t TYPE_EXPORT_DONE = 4;
constexpr std::uint8_t TYPE_RESTORE_BEGIN = 5;
constexpr std::uint8_t TYPE_RESTORE_DATA = 6;
constexpr std::uint8_t TYPE_RESTORE_CONFIRM = 7;
constexpr std::uint8_t TYPE_ACK = 8;
constexpr std::uint8_t TYPE_ERROR = 9;
constexpr std::size_t HEADER_SIZE = 12;
constexpr std::size_t MAX_PAYLOAD = 96;
constexpr std::size_t CHUNK_SIZE = MAX_PAYLOAD - 2;

struct Frame {
    std::uint8_t type = 0;
    std::uint16_t sequence = 0;
    std::array<std::uint8_t, MAX_PAYLOAD> payload{};
    std::uint16_t size = 0;
};

SwingMetro::LittleFsProgramStorage storage{false};
SwingMetro::ProgramMigrationController migration{storage};
SwingMetro::ProgramMigrationBackup backup;
std::size_t received = 0;
std::uint32_t expectedCrc = 0;
bool receiving = false;

auto readFrame16(const std::uint8_t* data) -> std::uint16_t {
    return static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8);
}

auto read32(const std::uint8_t* data) -> std::uint32_t {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

void writeFrame16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
}

void write32(std::uint8_t* data, std::uint32_t value) {
    for (std::uint8_t index = 0; index < 4; ++index) {
        data[index] = static_cast<std::uint8_t>(value >> (index * 8));
    }
}

auto frameCrc(const std::uint8_t* header, const std::uint8_t* payload, std::uint16_t size)
    -> std::uint32_t {
    std::array<std::uint8_t, 8 + MAX_PAYLOAD> data{};
    std::memcpy(data.data(), header, 8);
    if (size > 0) {
        std::memcpy(data.data() + 8, payload, size);
    }
    return SwingMetro::programMigrationCrc32(data.data(), 8 + size);
}

void sendFrame(std::uint8_t type, std::uint16_t sequence, const std::uint8_t* payload,
               std::uint16_t size) {
    std::array<std::uint8_t, HEADER_SIZE + MAX_PAYLOAD> wire{};
    wire[0] = 'S';
    wire[1] = 'M';
    wire[2] = PROTOCOL_VERSION;
    wire[3] = type;
    writeFrame16(wire.data() + 4, sequence);
    writeFrame16(wire.data() + 6, size);
    if (size > 0) {
        std::memcpy(wire.data() + HEADER_SIZE, payload, size);
    }
    write32(wire.data() + 8, frameCrc(wire.data(), payload, size));
    Serial.write(wire.data(), HEADER_SIZE + size);
    Serial.flush();
}

void sendStatus(std::uint8_t type, std::uint16_t sequence,
                SwingMetro::ProgramMigrationStatus status) {
    const auto value = static_cast<std::uint8_t>(status);
    sendFrame(type, sequence, &value, 1);
}

void handleFrame(const Frame& frame) {
    if (frame.type == TYPE_HELLO) {
        const std::array<std::uint8_t, 3> hello = {
            PROTOCOL_VERSION, static_cast<std::uint8_t>(SwingMetro::PROGRAM_MIGRATION_PAYLOAD_SIZE),
            static_cast<std::uint8_t>(SwingMetro::PROGRAM_MIGRATION_PAYLOAD_SIZE >> 8)};
        sendFrame(TYPE_HELLO, frame.sequence, hello.data(), hello.size());
        return;
    }
    if (frame.type == TYPE_EXPORT_CONFIRM && frame.size == 1 && frame.payload[0] == 1) {
#ifdef MIGRATION_RESTORE_ONLY
        sendStatus(TYPE_ERROR, frame.sequence, SwingMetro::ProgramMigrationStatus::InvalidBackup);
        return;
#endif
        const auto status = migration.exportBackup(backup);
        sendStatus(status == SwingMetro::ProgramMigrationStatus::Ok ? TYPE_ACK : TYPE_ERROR,
                   frame.sequence, status);
        if (status != SwingMetro::ProgramMigrationStatus::Ok) {
            return;
        }
        for (std::size_t offset = 0; offset < backup.bytes.size(); offset += CHUNK_SIZE) {
            const auto chunk =
                static_cast<std::uint16_t>(std::min(CHUNK_SIZE, backup.bytes.size() - offset));
            std::array<std::uint8_t, MAX_PAYLOAD> payload{};
            writeFrame16(payload.data(), static_cast<std::uint16_t>(offset));
            std::memcpy(payload.data() + 2, backup.bytes.data() + offset, chunk);
            sendFrame(TYPE_EXPORT_DATA, static_cast<std::uint16_t>(offset / CHUNK_SIZE),
                      payload.data(), chunk + 2);
        }
        sendStatus(TYPE_EXPORT_DONE, frame.sequence, SwingMetro::ProgramMigrationStatus::Ok);
        return;
    }
    if (frame.type == TYPE_RESTORE_BEGIN && frame.size == 4) {
#ifdef MIGRATION_EXPORT_ONLY
        sendStatus(TYPE_ERROR, frame.sequence, SwingMetro::ProgramMigrationStatus::InvalidBackup);
        return;
#endif
        expectedCrc = read32(frame.payload.data());
        received = 0;
        receiving = true;
        sendStatus(TYPE_ACK, frame.sequence, SwingMetro::ProgramMigrationStatus::Ok);
        return;
    }
    if (frame.type == TYPE_RESTORE_DATA && receiving && frame.size >= 2) {
        const auto offset = readFrame16(frame.payload.data());
        const auto dataSize = static_cast<std::size_t>(frame.size - 2);
        if (offset + dataSize <= received) {
            sendStatus(TYPE_ACK, frame.sequence, SwingMetro::ProgramMigrationStatus::Ok);
            return;
        }
        if (offset != received || received + dataSize > backup.bytes.size()) {
            sendStatus(TYPE_ERROR, frame.sequence,
                       SwingMetro::ProgramMigrationStatus::InvalidBackup);
            return;
        }
        std::memcpy(backup.bytes.data() + received, frame.payload.data() + 2, dataSize);
        received += dataSize;
        sendStatus(TYPE_ACK, frame.sequence, SwingMetro::ProgramMigrationStatus::Ok);
        return;
    }
    if (frame.type == TYPE_RESTORE_CONFIRM && frame.size == 1 && frame.payload[0] == 1 &&
        receiving && received == backup.bytes.size() &&
        SwingMetro::programMigrationCrc32(backup.bytes.data(), backup.bytes.size()) ==
            expectedCrc) {
        receiving = false;
        const auto status = migration.restoreBackup(backup);
        sendStatus(status == SwingMetro::ProgramMigrationStatus::Ok ? TYPE_ACK : TYPE_ERROR,
                   frame.sequence, status);
        return;
    }
    sendStatus(TYPE_ERROR, frame.sequence, SwingMetro::ProgramMigrationStatus::InvalidBackup);
}

void pollProtocol() {
    static std::array<std::uint8_t, HEADER_SIZE + MAX_PAYLOAD> wire{};
    static std::size_t used = 0;
    while (Serial.available() > 0) {
        const auto value = static_cast<std::uint8_t>(Serial.read());
        if (used == 0 && value != 'S') {
            continue;
        }
        if (used == 1 && value != 'M') {
            used = value == 'S' ? 1 : 0;
            continue;
        }
        wire[used++] = value;
        if (used < HEADER_SIZE) {
            continue;
        }
        const auto size = readFrame16(wire.data() + 6);
        if (wire[2] != PROTOCOL_VERSION || size > MAX_PAYLOAD) {
            used = 0;
            continue;
        }
        if (used != HEADER_SIZE + size) {
            continue;
        }
        if (read32(wire.data() + 8) == frameCrc(wire.data(), wire.data() + HEADER_SIZE, size)) {
            Frame frame;
            frame.type = wire[3];
            frame.sequence = readFrame16(wire.data() + 4);
            frame.size = size;
            std::memcpy(frame.payload.data(), wire.data() + HEADER_SIZE, size);
            handleFrame(frame);
        }
        used = 0;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    Serial.ignoreFlowControl(true);
}

void loop() { pollProtocol(); }
