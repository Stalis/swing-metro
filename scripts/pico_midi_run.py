#!/usr/bin/env python3
"""Run Swing Metro through Serial while capturing its USB MIDI output."""

from __future__ import annotations

import argparse
import csv
import pathlib
import statistics
import sys
import threading
import time
from dataclasses import dataclass
from typing import Any, Mapping

from pico_run_protocol import (
    CONTROL_PREFIX,
    diagnostics_header_for_row,
    find_serial_port,
    is_diagnostics_data_row,
    input_diagnostics_header_for_row,
    is_input_diagnostics_data_row,
    is_runtime_diagnostics_data_row,
    parse_diagnostics_row,
    parse_input_diagnostics_row,
    parse_runtime_diagnostics_row,
    runtime_diagnostics_header_for_row,
)


@dataclass(frozen=True)
class MidiEvent:
    sequence: int
    host_time_ns: int
    backend_delta_seconds: float
    message: tuple[int, ...]


def event_name(message: tuple[int, ...]) -> str:
    if not message:
        return "Empty"
    status = message[0]
    system_realtime = {
        0xF8: "Clock",
        0xF9: "Undefined F9",
        0xFA: "Start",
        0xFB: "Continue",
        0xFC: "Stop",
        0xFD: "Undefined FD",
        0xFE: "Active Sensing",
        0xFF: "System Reset",
    }
    if status in system_realtime:
        return system_realtime[status]
    kind = status & 0xF0
    if kind == 0x80:
        return "Note Off"
    if kind == 0x90:
        return "Note Off" if len(message) >= 3 and message[2] == 0 else "Note On"
    if kind == 0xA0:
        return "Polyphonic Key Pressure"
    if kind == 0xB0:
        return "Control Change"
    if kind == 0xC0:
        return "Program Change"
    if kind == 0xD0:
        return "Channel Pressure"
    if kind == 0xE0:
        return "Pitch Bend"
    if status == 0xF0:
        return "System Exclusive"
    return f"System 0x{status:02X}"


def choose_midi_port(ports: list[str], selector: str | None) -> int:
    if selector is not None:
        if selector.isdecimal():
            index = int(selector)
            if 0 <= index < len(ports):
                return index
            raise RuntimeError(f"MIDI port index {index} is out of range")
        matches = [index for index, name in enumerate(ports) if selector.casefold() in name.casefold()]
        if len(matches) == 1:
            return matches[0]
        raise RuntimeError(f"MIDI selector {selector!r} matched {[ports[index] for index in matches]}")

    preferred = [
        index
        for index, name in enumerate(ports)
        if "swing" in name.casefold() or "pico" in name.casefold()
    ]
    if len(preferred) == 1:
        return preferred[0]
    if len(ports) == 1:
        return 0
    raise RuntimeError(f"unable to choose one MIDI input port from {ports}; use --midi-port")


def percentile(values: list[float], percentile_value: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * percentile_value
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = position - lower
    return ordered[lower] + ((ordered[upper] - ordered[lower]) * fraction)


def analyze_events(events: list[MidiEvent], bpm: int) -> dict[str, int | float]:
    clocks = [event for event in events if event.message == (0xF8,)]
    starts = [event for event in events if event.message == (0xFA,)]
    stops = [event for event in events if event.message == (0xFC,)]
    intervals_us = [
        (current.host_time_ns - previous.host_time_ns) / 1000.0
        for previous, current in zip(clocks, clocks[1:])
    ]
    expected_us = 60_000_000.0 / (bpm * 24)
    absolute_jitter_us = [abs(value - expected_us) for value in intervals_us]
    absolute_clock_drift_us = 0.0
    if len(clocks) >= 2:
        absolute_clock_drift_us = abs(
            (clocks[-1].host_time_ns - clocks[0].host_time_ns) / 1000.0
            - (expected_us * (len(clocks) - 1))
        )
    long_intervals = [value for value in intervals_us if value > expected_us * 1.5]
    short_intervals = [value for value in intervals_us if value < expected_us * 0.5]
    estimated_missing = sum(max(0, round(value / expected_us) - 1) for value in long_intervals)
    return {
        "midi_event_count": len(events),
        "start_count": len(starts),
        "clock_count": len(clocks),
        "stop_count": len(stops),
        "expected_clock_interval_us": expected_us,
        "mean_clock_interval_us": statistics.fmean(intervals_us) if intervals_us else 0.0,
        "median_clock_interval_us": statistics.median(intervals_us) if intervals_us else 0.0,
        "p95_clock_interval_us": percentile(intervals_us, 0.95),
        "host_abs_jitter_p95_us": percentile(absolute_jitter_us, 0.95),
        "host_abs_jitter_p99_us": percentile(absolute_jitter_us, 0.99),
        "absolute_clock_drift_us": absolute_clock_drift_us,
        "min_clock_interval_us": min(intervals_us, default=0.0),
        "max_clock_interval_us": max(intervals_us, default=0.0),
        "long_interval_count": len(long_intervals),
        "short_interval_count": len(short_intervals),
        "estimated_missing_clock_count": estimated_missing,
    }


def add_firmware_summary(
    summary: Mapping[str, int | float], diagnostics: Mapping[str, int], diagnostics_version: int = 2
) -> dict[str, int | float | str]:
    fresh_firmware_counters = diagnostics["synchronous_start_publication_attempts"] == 1
    host_clock_difference: int | str = "not_comparable_cumulative_firmware_counters"
    if fresh_firmware_counters:
        host_clock_difference = diagnostics["outgoing_internal_f8_attempts"] - int(
            summary["clock_count"]
        )
    combined: dict[str, int | float | str] = dict(summary)
    combined.update(
        {
            "firmware_counters_fresh_for_run": int(fresh_firmware_counters),
            "firmware_f8_attempt_count": diagnostics["outgoing_internal_f8_attempts"],
            "firmware_successful_publication_count": diagnostics["successful_publications"],
            "firmware_successful_consumer_pop_count": diagnostics["successful_consumer_pops"],
            "host_clock_count_difference": host_clock_difference,
        }
    )
    if diagnostics_version == 2:
        combined.update(
            {
                "firmware_clock_stack_accepted_count": "unavailable_v2",
                "clock_attempt_minus_accepted": "unavailable_v2",
                "clock_accepted_minus_host": "unavailable_v2",
            }
        )
        return combined

    if diagnostics_version != 3:
        raise RuntimeError(f"unsupported diagnostics version {diagnostics_version}")
    accepted = diagnostics["delivery_clock_accepted"]
    difference: int | str = "not_comparable_cumulative_firmware_counters"
    if fresh_firmware_counters:
        difference = diagnostics["delivery_clock_attempts"] - accepted
    accepted_host_difference: int | str = "not_comparable_cumulative_firmware_counters"
    if fresh_firmware_counters:
        accepted_host_difference = accepted - int(summary["clock_count"])
    combined.update(
        {
            "firmware_clock_attempt_count": diagnostics["delivery_clock_attempts"],
            "firmware_clock_stack_accepted_count": accepted,
            "host_clock_count": int(summary["clock_count"]),
            "clock_attempt_minus_accepted": difference,
            "clock_accepted_minus_host": accepted_host_difference,
        }
    )
    return combined


def output_paths(
    prefix: pathlib.Path,
) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path, pathlib.Path]:
    return (
        pathlib.Path(f"{prefix}-midi.csv"),
        pathlib.Path(f"{prefix}-diagnostics.csv"),
        pathlib.Path(f"{prefix}-summary.csv"),
        pathlib.Path(f"{prefix}-runtime-diagnostics.csv"),
    )


def input_diagnostics_output_path(prefix: pathlib.Path) -> pathlib.Path:
    return pathlib.Path(f"{prefix}-input-diagnostics.csv")


def write_midi_csv(path: pathlib.Path, events: list[MidiEvent]) -> None:
    first_time_ns = events[0].host_time_ns if events else 0
    previous_clock_ns: int | None = None
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(
            (
                "sequence",
                "host_monotonic_ns",
                "elapsed_us",
                "backend_delta_seconds",
                "message_hex",
                "event",
                "channel",
                "data1",
                "data2",
                "clock_interval_us",
            )
        )
        for event in events:
            status = event.message[0] if event.message else 0
            channel = ((status & 0x0F) + 1) if 0x80 <= status <= 0xEF else ""
            interval_us: float | str = ""
            if event.message == (0xF8,):
                if previous_clock_ns is not None:
                    interval_us = (event.host_time_ns - previous_clock_ns) / 1000.0
                previous_clock_ns = event.host_time_ns
            writer.writerow(
                (
                    event.sequence,
                    event.host_time_ns,
                    (event.host_time_ns - first_time_ns) / 1000.0,
                    event.backend_delta_seconds,
                    " ".join(f"{byte:02X}" for byte in event.message),
                    event_name(event.message),
                    channel,
                    event.message[1] if len(event.message) > 1 else "",
                    event.message[2] if len(event.message) > 2 else "",
                    interval_us,
                )
            )


def write_summary_csv(path: pathlib.Path, summary: dict[str, int | float | str]) -> None:
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(("metric", "value"))
        writer.writerows(summary.items())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="Serial port; auto-detected when omitted")
    parser.add_argument("--midi-port", help="MIDI input port index or unique name substring")
    parser.add_argument("--list-midi-ports", action="store_true")
    parser.add_argument("--duration-seconds", type=float, default=244.0)
    parser.add_argument("--bpm", type=int, default=68)
    parser.add_argument("--swing", type=int, default=50)
    parser.add_argument("--output-prefix", type=pathlib.Path)
    return parser.parse_args()


def load_rtmidi() -> Any:
    try:
        import rtmidi
    except ImportError as error:
        raise RuntimeError(
            "python-rtmidi is required; install scripts/requirements-hardware.txt"
        ) from error
    return rtmidi


def load_serial() -> Any:
    try:
        import serial
    except ImportError as error:
        raise RuntimeError(
            "pyserial is required; install scripts/requirements-hardware.txt"
        ) from error
    return serial


def main() -> int:
    args = parse_args()
    rtmidi = load_rtmidi()
    serial = load_serial()
    midi_in = rtmidi.MidiIn()
    ports = midi_in.get_ports()
    if args.list_midi_ports:
        for index, name in enumerate(ports):
            print(f"{index}: {name}")
        return 0
    if args.output_prefix is None:
        raise RuntimeError("--output-prefix is required unless --list-midi-ports is used")

    midi_path, diagnostics_path, summary_path, runtime_diagnostics_path = output_paths(args.output_prefix)
    input_diagnostics_path = input_diagnostics_output_path(args.output_prefix)
    output_files = (
        midi_path,
        diagnostics_path,
        summary_path,
        input_diagnostics_path,
        runtime_diagnostics_path,
    )
    existing = [path for path in output_files if path.exists()]
    if existing:
        raise RuntimeError(f"refusing to overwrite {existing}")
    for path in output_files:
        path.parent.mkdir(parents=True, exist_ok=True)

    midi_port_index = choose_midi_port(ports, args.midi_port)
    serial_port = find_serial_port(args.port)
    duration_ms = round(args.duration_seconds * 1000)
    events: list[MidiEvent] = []
    event_lock = threading.Lock()

    def receive_midi(event_data: tuple[list[int], float], _: Any = None) -> None:
        message, delta_seconds = event_data
        with event_lock:
            events.append(
                MidiEvent(len(events) + 1, time.monotonic_ns(), delta_seconds, tuple(message))
            )

    midi_in.ignore_types(sysex=False, timing=False, active_sense=False)
    midi_in.set_callback(receive_midi)
    midi_in.open_port(midi_port_index)
    print(f"MIDI input: {ports[midi_port_index]}", flush=True)
    print(f"Serial: {serial_port}", flush=True)

    row: str | None = None
    input_row: str | None = None
    runtime_row: str | None = None
    completed = False
    timeout_at = time.monotonic() + args.duration_seconds + 30.0
    try:
        with serial.Serial(serial_port, 115200, timeout=0.25) as connection:
            connection.reset_input_buffer()
            connection.write(f"RUN {duration_ms} {args.bpm} {args.swing}\n".encode("ascii"))
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
                    completed = True
                    break
    finally:
        time.sleep(0.1)
        midi_in.close_port()

    if not completed:
        raise RuntimeError("timed run did not report completion")
    if row is None or input_row is None or runtime_row is None:
        raise RuntimeError("run completed without diagnostics, input, and runtime diagnostics rows")
    diagnostics_version, diagnostics = parse_diagnostics_row(row)
    input_diagnostics = parse_input_diagnostics_row(input_row)
    runtime_diagnostics = parse_runtime_diagnostics_row(runtime_row)
    with event_lock:
        captured_events = list(events)

    summary = analyze_events(captured_events, args.bpm)
    summary_with_firmware = add_firmware_summary(summary, diagnostics, diagnostics_version)
    summary_with_firmware.update(
        {
            "firmware_input_max_actual_encoder_sample_interval_us": input_diagnostics[
                "max_actual_encoder_sample_interval_us"
            ],
            "firmware_runtime_lv_timer_handler_count": runtime_diagnostics["lv_timer_handler_count"],
            "firmware_runtime_lv_timer_handler_inclusive_total_us": runtime_diagnostics[
                "lv_timer_handler_inclusive_total_us"
            ],
            "firmware_runtime_lv_timer_handler_inclusive_max_us": runtime_diagnostics[
                "lv_timer_handler_inclusive_max_us"
            ],
            "firmware_runtime_display_flush_count": runtime_diagnostics["display_flush_count"],
            "firmware_runtime_display_flush_inclusive_total_us": runtime_diagnostics[
                "display_flush_inclusive_total_us"
            ],
            "firmware_runtime_display_flush_inclusive_max_us": runtime_diagnostics[
                "display_flush_inclusive_max_us"
            ],
            "firmware_runtime_encoder_sample_window_max_interval_us": runtime_diagnostics[
                "encoder_sample_window_max_interval_us"
            ],
            "firmware_runtime_encoder_sample_window_intervals_above_1250_us": runtime_diagnostics[
                "encoder_sample_window_intervals_above_1250_us"
            ],
            "firmware_input_encoder_sample_intervals_above_1250_us": input_diagnostics[
                "encoder_sample_intervals_above_1250_us"
            ],
        }
    )
    diagnostics_path.write_text(f"{diagnostics_header_for_row(row)}\n{row}\n", encoding="utf-8")
    input_diagnostics_path.write_text(
        f"{input_diagnostics_header_for_row(input_row)}\n{input_row}\n", encoding="utf-8"
    )
    runtime_diagnostics_path.write_text(
        f"{runtime_diagnostics_header_for_row(runtime_row)}\n{runtime_row}\n", encoding="utf-8"
    )
    write_midi_csv(midi_path, captured_events)
    write_summary_csv(summary_path, summary_with_firmware)
    print(f"saved {midi_path}")
    print(f"saved {diagnostics_path}")
    print(f"saved {input_diagnostics_path}")
    print(f"saved {runtime_diagnostics_path}")
    print(f"saved {summary_path}")
    print(
        f"Clock: host={summary_with_firmware['clock_count']}, "
        f"firmware={summary_with_firmware['firmware_f8_attempt_count']}, "
        f"difference={summary_with_firmware['host_clock_count_difference']}, "
        f"long intervals={summary['long_interval_count']}",
        flush=True,
    )
    if not summary_with_firmware["firmware_counters_fresh_for_run"]:
        print("warning: reboot Pico before a measured run; firmware counters are cumulative")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
