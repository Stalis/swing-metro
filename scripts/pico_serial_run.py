#!/usr/bin/env python3
"""Run a timed Swing Metro hardware capture and save its Serial diagnostics."""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

from pico_run_protocol import (
    diagnostics_header_for_row,
    input_diagnostics_header_for_row,
    runtime_diagnostics_header_for_row,
    wait_for_serial_port,
)
from pico_run_report import (
    TimedRunCapture,
    build_timed_run_report,
    load_report_metadata,
    report_output_path,
    validate_fault_metadata,
    write_timed_run_report,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port")
    parser.add_argument("--duration-seconds", type=float, default=244.0)
    parser.add_argument("--bpm", type=int, default=68)
    parser.add_argument("--swing", type=int, default=50)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--metadata", type=pathlib.Path)
    parser.add_argument(
        "--fault-scenario",
        choices=("baseline", "retry_first_clock", "sustained_backpressure", "deterministic_disconnect"),
    )
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
    report_path = report_output_path(args.output)
    existing = [
        path for path in (args.output, input_path, runtime_path, report_path) if path.exists()
    ]
    if existing:
        raise RuntimeError(f"refusing to overwrite {existing}")

    metadata = load_report_metadata(args.metadata)
    validate_fault_metadata(metadata, args.fault_scenario)
    serial = load_serial()
    port = wait_for_serial_port(args.port)
    capture = TimedRunCapture(duration_ms, args.bpm, args.swing, args.fault_scenario)
    timeout_at = time.monotonic() + args.duration_seconds + 30.0
    with serial.Serial(port, 115200, timeout=0.25) as connection:
        connection.reset_input_buffer()
        if args.fault_scenario is not None:
            connection.write(f"FAULT {args.fault_scenario}\n".encode("ascii"))
            connection.flush()
            while time.monotonic() < timeout_at and not capture.fault_acknowledged:
                raw = connection.readline()
                if raw:
                    line = raw.decode("utf-8", "replace").strip()
                    print(line, flush=True)
                    capture.consume(line)
            if not capture.fault_acknowledged:
                raise RuntimeError("timed out waiting for fault acknowledgement")
        command = f"RUN {duration_ms} {args.bpm} {args.swing}\n"
        connection.write(command.encode("ascii"))
        connection.flush()
        while time.monotonic() < timeout_at:
            raw = connection.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            print(line, flush=True)
            capture.consume(line)
            if capture.completed:
                break

    capture.require_complete()
    assert capture.diagnostics_row is not None
    assert capture.input_row is not None
    assert capture.runtime_row is not None
    row = capture.diagnostics_row
    input_row = capture.input_row
    runtime_row = capture.runtime_row
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(f"{diagnostics_header_for_row(row)}\n{row}\n", encoding="utf-8")
    input_path.write_text(
        f"{input_diagnostics_header_for_row(input_row)}\n{input_row}\n", encoding="utf-8"
    )
    runtime_path.write_text(
        f"{runtime_diagnostics_header_for_row(runtime_row)}\n{runtime_row}\n", encoding="utf-8"
    )
    report = build_timed_run_report(
        capture=capture,
        metadata=metadata,
        serial_port=port,
        midi_port=None,
        host_midi=None,
    )
    write_timed_run_report(report_path, report)
    print(f"saved {args.output}")
    print(f"saved {input_path}")
    print(f"saved {runtime_path}")
    print(f"saved {report_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
