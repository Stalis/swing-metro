#!/usr/bin/env python3
"""Run a timed Swing Metro hardware capture and save its Serial diagnostics."""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

from pico_run_protocol import (
    CONTROL_PREFIX,
    diagnostics_header_for_row,
    find_serial_port,
    is_diagnostics_data_row,
    input_diagnostics_header_for_row,
    is_input_diagnostics_data_row,
    is_runtime_diagnostics_data_row,
    runtime_diagnostics_header_for_row,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port")
    parser.add_argument("--duration-seconds", type=float, default=244.0)
    parser.add_argument("--bpm", type=int, default=68)
    parser.add_argument("--swing", type=int, default=50)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    return parser.parse_args()


def input_output_path(path: pathlib.Path) -> pathlib.Path:
    return path.with_name(f"{path.stem}-input{path.suffix}")


def runtime_output_path(path: pathlib.Path) -> pathlib.Path:
    return path.with_name(f"{path.stem}-runtime{path.suffix}")


def load_serial():
    try:
        import serial
    except ImportError as error:
        raise RuntimeError(
            "pyserial is required; install scripts/requirements-hardware.txt"
        ) from error
    return serial


def main() -> int:
    args = parse_args()
    duration_ms = round(args.duration_seconds * 1000)
    input_path = input_output_path(args.output)
    runtime_path = runtime_output_path(args.output)
    existing = [path for path in (args.output, input_path, runtime_path) if path.exists()]
    if existing:
        raise RuntimeError(f"refusing to overwrite {existing}")

    serial = load_serial()
    port = find_serial_port(args.port)
    row: str | None = None
    input_row: str | None = None
    runtime_row: str | None = None
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
            elif is_input_diagnostics_data_row(line):
                input_row = line
            elif is_runtime_diagnostics_data_row(line):
                runtime_row = line
            elif line == f"{CONTROL_PREFIX},run_complete":
                break

    if row is None or input_row is None or runtime_row is None:
        raise RuntimeError("run completed without diagnostics, input, and runtime diagnostics rows")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(f"{diagnostics_header_for_row(row)}\n{row}\n", encoding="utf-8")
    input_path.write_text(
        f"{input_diagnostics_header_for_row(input_row)}\n{input_row}\n", encoding="utf-8"
    )
    runtime_path.write_text(
        f"{runtime_diagnostics_header_for_row(runtime_row)}\n{runtime_row}\n", encoding="utf-8"
    )
    print(f"saved {args.output}")
    print(f"saved {input_path}")
    print(f"saved {runtime_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
