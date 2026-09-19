#!/usr/bin/env python3
"""Run a timed Swing Metro hardware capture and save its Serial diagnostics."""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

import serial

from pico_run_protocol import (
    CONTROL_PREFIX,
    diagnostics_header_for_row,
    find_serial_port,
    is_diagnostics_data_row,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port")
    parser.add_argument("--duration-seconds", type=float, default=244.0)
    parser.add_argument("--bpm", type=int, default=68)
    parser.add_argument("--swing", type=int, default=50)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    duration_ms = round(args.duration_seconds * 1000)
    if args.output.exists():
        raise RuntimeError(f"refusing to overwrite {args.output}")

    port = find_serial_port(args.port)
    row: str | None = None
    timeout_at = time.monotonic() + args.duration_seconds + 30.0
    with serial.Serial(port, 115200, timeout=0.25) as connection:
        connection.reset_input_buffer()
        command = f"RUN {duration_ms} {args.bpm} {args.swing}\n"
        connection.write(command.encode("ascii"))
        connection.flush()
        while time.monotonic() < timeout_at:
            raw = connection.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            print(line, flush=True)
            if is_diagnostics_data_row(line):
                row = line
            elif line == f"{CONTROL_PREFIX},run_complete":
                break

    if row is None:
        raise RuntimeError("run completed without a diagnostics row")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(f"{diagnostics_header_for_row(row)}\n{row}\n", encoding="utf-8")
    print(f"saved {args.output}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, serial.SerialException) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
