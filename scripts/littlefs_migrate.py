#!/usr/bin/env python3
"""Export a 64 KB Swing Metro LittleFS backup or restore it to a 1 MB image."""

import argparse
import fcntl
import os
import struct
import sys
import termios
import time
import hashlib
import zlib

MAGIC = b"SM"
VERSION = 1
HEADER = struct.Struct("<2sBBHHI")
SMBK = struct.Struct("<4sBBHIII")
PAYLOAD_SIZE = 1 + 17 * 2 * (1 + 2 + 271)
CHUNK = 94
HELLO, EXPORT_CONFIRM, EXPORT_DATA, EXPORT_DONE, RESTORE_BEGIN, RESTORE_DATA, RESTORE_CONFIRM, ACK, ERROR = range(1, 10)


class ProtocolError(RuntimeError):
    pass


class Port:
    def __init__(self, name):
        self.fd = os.open(name, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        attrs[2] |= termios.CLOCAL | termios.CREAD
        attrs[4] = attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        modem_bits = struct.pack("I", termios.TIOCM_DTR | termios.TIOCM_RTS)
        fcntl.ioctl(self.fd, termios.TIOCMBIS, modem_bits)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.buffer = bytearray()

    def close(self):
        os.close(self.fd)

    def send(self, kind, sequence, payload=b""):
        prefix = HEADER.pack(MAGIC, VERSION, kind, sequence, len(payload), 0)[:8]
        wire = prefix + struct.pack("<I", zlib.crc32(prefix + payload) & 0xFFFFFFFF) + payload
        os.write(self.fd, wire)

    def receive(self, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                self.buffer.extend(os.read(self.fd, 512))
            except BlockingIOError:
                pass
            while len(self.buffer) >= HEADER.size:
                start = self.buffer.find(MAGIC)
                if start < 0:
                    self.buffer.clear()
                    break
                if start:
                    del self.buffer[:start]
                if len(self.buffer) < HEADER.size:
                    break
                magic, version, kind, sequence, size, crc = HEADER.unpack(self.buffer[:HEADER.size])
                if version != VERSION or size > 96:
                    del self.buffer[0]
                    continue
                total = HEADER.size + size
                if len(self.buffer) < total:
                    break
                payload = bytes(self.buffer[HEADER.size:total])
                del self.buffer[:total]
                prefix = HEADER.pack(magic, version, kind, sequence, size, 0)[:8]
                if zlib.crc32(prefix + payload) & 0xFFFFFFFF == crc:
                    return kind, sequence, payload
            time.sleep(0.01)
        raise ProtocolError("timeout waiting for device")

    def request(self, kind, sequence, payload=b""):
        for _ in range(3):
            self.send(kind, sequence, payload)
            try:
                response = self.receive()
            except ProtocolError:
                continue
            if response[1] == sequence and response[0] in (ACK, ERROR, HELLO):
                if response[0] == ERROR:
                    raise ProtocolError(f"device rejected request, status {response[2][0] if response[2] else '?'}")
                return response
        raise ProtocolError("device did not acknowledge request after 3 attempts")


def validate_payload(payload):
    if len(payload) != PAYLOAD_SIZE or payload[0] != VERSION:
        raise ProtocolError("invalid migration payload")
    for offset in range(1, len(payload), 274):
        present, size = payload[offset], struct.unpack_from("<H", payload, offset + 1)[0]
        if present > 1 or size > 271:
            raise ProtocolError("invalid copy record")


def read_backup(path):
    blob = open(path, "rb").read()
    if len(blob) < SMBK.size:
        raise ProtocolError("backup is shorter than SMBK header")
    magic, version, _, header_size, payload_size, payload_crc, header_crc = SMBK.unpack(blob[:SMBK.size])
    if magic != b"SMBK" or version != VERSION or header_size != SMBK.size:
        raise ProtocolError("unsupported SMBK header")
    if zlib.crc32(blob[:16]) & 0xFFFFFFFF != header_crc or len(blob) != SMBK.size + payload_size:
        raise ProtocolError("SMBK header CRC or size mismatch")
    payload = blob[SMBK.size:]
    if zlib.crc32(payload) & 0xFFFFFFFF != payload_crc:
        raise ProtocolError("SMBK payload CRC mismatch")
    validate_payload(payload)
    return payload


def write_backup(path, payload):
    validate_payload(payload)
    prefix = struct.pack("<4sBBHII", b"SMBK", VERSION, 0, SMBK.size, len(payload), zlib.crc32(payload) & 0xFFFFFFFF)
    with open(path, "wb") as output:
        output.write(prefix + struct.pack("<I", zlib.crc32(prefix) & 0xFFFFFFFF) + payload)


def backup_fingerprint(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def hello(port):
    kind, _, payload = port.request(HELLO, 1)
    if kind != HELLO or len(payload) != 3 or payload[0] != VERSION or struct.unpack("<H", payload[1:])[0] != PAYLOAD_SIZE:
        raise ProtocolError("device protocol or payload size does not match")


def export_backup(port, output):
    port.request(EXPORT_CONFIRM, 2, b"\x01")
    payload = bytearray(PAYLOAD_SIZE)
    received = 0
    while True:
        kind, _, data = port.receive(10)
        if kind == EXPORT_DONE:
            break
        if kind != EXPORT_DATA or len(data) < 2:
            raise ProtocolError("unexpected export frame")
        offset = struct.unpack("<H", data[:2])[0]
        if offset != received or offset + len(data) - 2 > PAYLOAD_SIZE:
            raise ProtocolError("invalid export chunk")
        payload[offset:offset + len(data) - 2] = data[2:]
        received += len(data) - 2
    if received != PAYLOAD_SIZE:
        raise ProtocolError("incomplete export")
    write_backup(output, bytes(payload))


def restore_backup(port, payload):
    port.request(RESTORE_BEGIN, 2, struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF))
    sequence = 3
    for offset in range(0, len(payload), CHUNK):
        port.request(RESTORE_DATA, sequence, struct.pack("<H", offset) + payload[offset:offset + CHUNK])
        sequence += 1
    port.request(RESTORE_CONFIRM, sequence, b"\x01")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="CDC device, for example /dev/cu.usbmodem123")
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--export", metavar="SMBK")
    actions.add_argument("--restore", metavar="SMBK")
    actions.add_argument("--validate", metavar="SMBK")
    parser.add_argument("--yes-export", action="store_true", help="confirm read-only backup")
    parser.add_argument("--yes-restore", action="store_true", help="confirm writing the empty 1 MB target")
    parser.add_argument("--prepared-target", metavar="SHA256", help="backup SHA-256 typed after destructive target preparation")
    args = parser.parse_args()
    if args.export and not args.yes_export:
        parser.error("--export requires --yes-export")
    if args.restore and not args.yes_restore:
        parser.error("--restore requires --yes-restore")
    if args.restore and not args.prepared_target:
        parser.error("--restore requires --prepared-target SHA256")
    if args.validate:
        read_backup(args.validate)
        print(f"valid SMBK: {args.validate}\nsha256: {backup_fingerprint(args.validate)}")
        return
    if args.restore and args.prepared_target.lower() != backup_fingerprint(args.restore):
        parser.error("--prepared-target must exactly match the backup SHA-256")
    if not args.port:
        parser.error("--export and --restore require --port")
    port = Port(args.port)
    try:
        hello(port)
        if args.export:
            export_backup(port, args.export)
            print(f"backup written: {args.export}\nsha256: {backup_fingerprint(args.export)}")
        else:
            restore_backup(port, read_backup(args.restore))
            print("restore verified by device")
    finally:
        port.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ProtocolError) as error:
        print(f"migration failed: {error}", file=sys.stderr)
        sys.exit(1)
