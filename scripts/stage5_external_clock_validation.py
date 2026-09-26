#!/usr/bin/env python3
"""Capture and validate the Stage 5.5.2 external MIDI Clock scenario."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import platform
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from typing import Any, Callable, Mapping

from pico_midi_run import (
    MidiEvent,
    analyze_events,
    load_rtmidi,
    load_serial,
    wait_for_midi_port,
    write_midi_csv,
)
from pico_run_protocol import (
    diagnostics_header_for_row,
    input_diagnostics_header_for_row,
    parse_diagnostics_row,
    parse_input_diagnostics_row,
    parse_runtime_diagnostics_row,
    runtime_diagnostics_header_for_row,
    wait_for_serial_port,
)
from pico_run_report import TimedRunCapture
from stage5_load_matrix_run import (
    allocate_output_dir,
    git_metadata,
    platformio_environment_config,
    platformio_packages,
    sha256,
)


PRODUCTION_ENVIRONMENT = "rpipico2"
PRODUCTION_ELF = pathlib.Path(".pio/build/rpipico2/firmware.elf")
DEFAULT_OUTPUT_ROOT = pathlib.Path("data/stage5-5-external-runs")
RUN_NAME = "stage5-5-external-clock"
MIDI_BPM = 120
MIDI_SWING = 50
MIDI_CLOCKS_PER_QUARTER = 24
LOSS_PAUSE_NS = 400_000_000
POST_STIMULUS_MARGIN_NS = 1_000_000_000


@dataclass(frozen=True)
class PlannedMidiEvent:
    sequence: int
    phase: str
    scheduled_offset_ns: int
    message: tuple[int, ...]


@dataclass(frozen=True)
class SentMidiEvent:
    sequence: int
    phase: str
    scheduled_offset_ns: int
    host_send_ns: int
    message: tuple[int, ...]


def _append_clocks(
    events: list[PlannedMidiEvent],
    phase: str,
    cursor_ns: int,
    count: int,
    intervals_ns: tuple[int, ...],
) -> int:
    for index in range(count):
        cursor_ns += intervals_ns[index % len(intervals_ns)]
        events.append(PlannedMidiEvent(len(events) + 1, phase, cursor_ns, (0xF8,)))
    return cursor_ns


def build_stimulus() -> list[PlannedMidiEvent]:
    """Return one deterministic Start/Clock/Stop/Continue/loss/relock sequence."""
    interval_ns = round(60_000_000_000 / (MIDI_BPM * MIDI_CLOCKS_PER_QUARTER))
    jitter_ns = (-1_000_000, 1_000_000, -500_000, 500_000)
    jittered_intervals = tuple(interval_ns + offset for offset in jitter_ns)
    events: list[PlannedMidiEvent] = []
    cursor_ns = 100_000_000
    events.append(PlannedMidiEvent(1, "start", cursor_ns, (0xFA,)))
    cursor_ns = _append_clocks(events, "steady_clock", cursor_ns, 96, (interval_ns,))
    cursor_ns = _append_clocks(events, "jitter_clock", cursor_ns, 96, jittered_intervals)
    cursor_ns += interval_ns
    events.append(PlannedMidiEvent(len(events) + 1, "stop_before_continue", cursor_ns, (0xFC,)))
    cursor_ns += 100_000_000
    events.append(PlannedMidiEvent(len(events) + 1, "continue_after_stop", cursor_ns, (0xFB,)))
    cursor_ns = _append_clocks(events, "resumed_clock", cursor_ns, 48, (interval_ns,))
    cursor_ns += LOSS_PAUSE_NS
    events.append(PlannedMidiEvent(len(events) + 1, "relock_clock", cursor_ns, (0xF8,)))
    cursor_ns += 5_000_000
    events.append(PlannedMidiEvent(len(events) + 1, "continue_after_loss", cursor_ns, (0xFB,)))
    cursor_ns = _append_clocks(events, "relocked_clock", cursor_ns, 96, (interval_ns,))
    cursor_ns += interval_ns
    events.append(PlannedMidiEvent(len(events) + 1, "final_stop", cursor_ns, (0xFC,)))
    return events


def stimulus_duration_ms(events: list[PlannedMidiEvent]) -> int:
    return (events[-1].scheduled_offset_ns + POST_STIMULUS_MARGIN_NS + 999_999) // 1_000_000


def send_stimulus(
    midi_out: Any,
    events: list[PlannedMidiEvent],
    *,
    monotonic_ns: Callable[[], int] = time.monotonic_ns,
    sleep: Callable[[float], None] = time.sleep,
) -> list[SentMidiEvent]:
    origin_ns = monotonic_ns()
    sent: list[SentMidiEvent] = []
    for event in events:
        target_ns = origin_ns + event.scheduled_offset_ns
        while True:
            remaining_ns = target_ns - monotonic_ns()
            if remaining_ns <= 0:
                break
            sleep(remaining_ns / 1_000_000_000)
        host_send_ns = monotonic_ns()
        midi_out.send_message(list(event.message))
        sent.append(
            SentMidiEvent(
                event.sequence,
                event.phase,
                event.scheduled_offset_ns,
                host_send_ns,
                event.message,
            )
        )
    return sent


def _one_send_time(events: list[SentMidiEvent], phase: str) -> int:
    matches = [event.host_send_ns for event in events if event.phase == phase]
    if len(matches) != 1:
        raise RuntimeError(f"expected exactly one sent event for phase {phase!r}")
    return matches[0]


def stimulus_metrics(events: list[SentMidiEvent]) -> dict[str, int | float]:
    clocks = [event for event in events if event.message == (0xF8,)]
    schedule_error_us = [
        (event.host_send_ns - events[0].host_send_ns)
        - (event.scheduled_offset_ns - events[0].scheduled_offset_ns)
        for event in events
    ]
    last_pre_loss = max(
        event.host_send_ns for event in events if event.phase == "resumed_clock"
    )
    relock = _one_send_time(events, "relock_clock")
    return {
        "event_count": len(events),
        "clock_count": len(clocks),
        "start_count": sum(event.message == (0xFA,) for event in events),
        "continue_count": sum(event.message == (0xFB,) for event in events),
        "stop_count": sum(event.message == (0xFC,) for event in events),
        "actual_loss_pause_us": (relock - last_pre_loss) / 1000.0,
        "max_absolute_schedule_error_us": max(
            (abs(value) / 1000.0 for value in schedule_error_us), default=0.0
        ),
    }


def _note_on_times(events: list[MidiEvent]) -> list[int]:
    return [
        event.host_time_ns
        for event in events
        if len(event.message) == 3
        and event.message[0] & 0xF0 == 0x90
        and event.message[2] != 0
    ]


def evaluate_acceptance(
    received: list[MidiEvent],
    sent: list[SentMidiEvent],
    diagnostics: Mapping[str, int],
) -> dict[str, Any]:
    host = analyze_events(received, MIDI_BPM)
    note_on_times = _note_on_times(received)
    pre_loss_boundary = max(
        event.host_send_ns for event in sent if event.phase == "resumed_clock"
    )
    post_relock_boundary = _one_send_time(sent, "continue_after_loss")
    note_on_before_loss = sum(timestamp <= pre_loss_boundary for timestamp in note_on_times)
    note_on_after_relock = sum(timestamp >= post_relock_boundary for timestamp in note_on_times)
    echoed_realtime_count = sum(
        event.message in ((0xF8,), (0xFA,), (0xFB,), (0xFC,)) for event in received
    )
    zero_fields = (
        "delivery_clock_retry_later",
        "delivery_clock_disconnected",
        "delivery_transport_retry_later",
        "delivery_transport_disconnected",
        "delivery_note_retry_later",
        "delivery_note_disconnected",
        "terminal_note_off_abandoned_count",
        "terminal_stop_abandoned_count",
        "note_on_expired_count",
        "failed_publications",
        "tick_queue_overflows",
        "internal_tick_queue_overflows",
        "outbox_capacity_failures",
        "delivery_capacity_safety_stops",
        "retry_window_safety_stops",
    )
    checks = {
        "no_realtime_echo": echoed_realtime_count == 0,
        "external_loss_exactly_once": diagnostics["session_ends_external_clock_lost"] == 1,
        "two_explicit_stops_plus_start_cleanup": diagnostics["session_ends_stop"] == 3,
        "one_clean_start": diagnostics["explicit_clean_starts"] == 1,
        "start_and_two_continues_advanced_sessions": diagnostics[
            "session_generation_advances"
        ]
        == 3,
        "no_outbound_clock_delivery": diagnostics["delivery_clock_attempts"] == 0
        and diagnostics["delivery_clock_accepted"] == 0
        and diagnostics["outgoing_internal_f8_attempts"] == 0,
        "no_outbound_transport_delivery": diagnostics["delivery_transport_attempts"] == 0
        and diagnostics["delivery_transport_accepted"] == 0,
        "balanced_host_notes": host["note_on_count"] > 0
        and host["note_on_count"] == host["note_off_count"]
        and host["unmatched_note_off_count"] == 0
        and host["dangling_note_on_count"] == 0,
        "device_host_note_counts_match": diagnostics["delivery_note_accepted"]
        == host["note_on_count"] + host["note_off_count"],
        "note_delivery_first_attempt": diagnostics["delivery_note_attempts"]
        == diagnostics["delivery_note_accepted"],
        "notes_before_loss": note_on_before_loss > 0,
        "notes_after_relock": note_on_after_relock > 0,
        "delivery_and_queue_faults_zero": all(diagnostics[field] == 0 for field in zero_fields),
        "outbox_empty_at_end": diagnostics["current_outbox_depth"] == 0,
    }
    return {
        "result": "pass" if all(checks.values()) else "fail",
        "checks": checks,
        "observations": {
            "echoed_realtime_count": echoed_realtime_count,
            "note_on_before_loss": note_on_before_loss,
            "note_on_after_relock": note_on_after_relock,
        },
    }


def write_stimulus_csv(path: pathlib.Path, events: list[SentMidiEvent]) -> None:
    origin_ns = events[0].host_send_ns if events else 0
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(
            (
                "sequence",
                "phase",
                "scheduled_offset_us",
                "host_send_ns",
                "actual_offset_us",
                "schedule_error_us",
                "message_hex",
            )
        )
        for event in events:
            actual_offset_ns = event.host_send_ns - origin_ns
            relative_schedule_ns = event.scheduled_offset_ns - events[0].scheduled_offset_ns
            writer.writerow(
                (
                    event.sequence,
                    event.phase,
                    event.scheduled_offset_ns / 1000.0,
                    event.host_send_ns,
                    actual_offset_ns / 1000.0,
                    (actual_offset_ns - relative_schedule_ns) / 1000.0,
                    " ".join(f"{byte:02X}" for byte in event.message),
                )
            )


def write_exact_json(path: pathlib.Path, value: Any) -> None:
    encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if path.exists() and path.read_text(encoding="utf-8") != encoded:
        raise RuntimeError(f"refusing to replace mismatched file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(encoded, encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pattern-confirmed", action="store_true")
    parser.add_argument("--output-root", type=pathlib.Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--port", help="Serial port; auto-detected when omitted")
    parser.add_argument("--midi-input-port", help="MIDI input index or unique name substring")
    parser.add_argument("--midi-output-port", help="MIDI output index or unique name substring")
    parser.add_argument("--pio", default="pio")
    parser.add_argument("--post-upload-settle-seconds", type=float, default=3.0)
    parser.add_argument("--usb-topology", default="direct USB connection; not independently verified")
    parser.add_argument("--list-midi-ports", action="store_true")
    return parser.parse_args()


def _metadata(
    revision: str,
    dirty: bool,
    binary_sha256: str,
    build_flags: list[str],
    toolchain: str,
    dependencies: list[str],
    usb_topology: str,
) -> dict[str, Any]:
    return {
        "run_id": RUN_NAME,
        "scenario": {
            "id": RUN_NAME,
            "interaction": "none during capture",
            "clock_mode": "external",
            "program": {"steps": "all 16 enabled", "gate_percent_by_step": [100] * 16},
        },
        "instrumentation": {"stage5": "enabled"},
        "firmware": {
            "source_revision": revision,
            "working_tree_dirty": dirty,
            "binary_sha256": binary_sha256,
            "platformio_environment": PRODUCTION_ENVIRONMENT,
            "build_flags": build_flags,
            "toolchain": toolchain,
            "dependencies": dependencies,
        },
        "hardware": {
            "board": "Raspberry Pi Pico 2 W",
            "cpu_frequency_hz": 150_000_000,
            "usb_topology": usb_topology,
        },
        "host": {
            "os": platform.platform(),
            "architecture": platform.machine(),
            "midi_api": "CoreMIDI" if platform.system() == "Darwin" else "python-rtmidi",
            "notes": "automated production external Clock validation",
        },
    }


def _read_until(
    connection: Any,
    capture: TimedRunCapture,
    predicate: Callable[[], bool],
    timeout_at: float,
) -> None:
    while time.monotonic() < timeout_at and not predicate():
        raw = connection.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", "replace").strip()
        print(line, flush=True)
        capture.consume(line)
    if not predicate():
        raise RuntimeError("timed out waiting for device capture protocol")


def _capture(
    args: argparse.Namespace,
    output_dir: pathlib.Path,
    metadata: Mapping[str, Any],
    planned: list[PlannedMidiEvent],
) -> dict[str, Any]:
    rtmidi = load_rtmidi()
    serial = load_serial()
    midi_in = rtmidi.MidiIn()
    midi_out = rtmidi.MidiOut()
    input_index, input_ports = wait_for_midi_port(midi_in, args.midi_input_port)
    output_index, output_ports = wait_for_midi_port(midi_out, args.midi_output_port)
    serial_port = wait_for_serial_port(args.port)
    duration_ms = stimulus_duration_ms(planned)
    prefix = output_dir / RUN_NAME
    paths = {
        "midi": pathlib.Path(f"{prefix}-midi.csv"),
        "stimulus": pathlib.Path(f"{prefix}-stimulus.csv"),
        "diagnostics": pathlib.Path(f"{prefix}-diagnostics.csv"),
        "input": pathlib.Path(f"{prefix}-input-diagnostics.csv"),
        "runtime": pathlib.Path(f"{prefix}-runtime-diagnostics.csv"),
        "report": pathlib.Path(f"{prefix}-report.json"),
    }
    existing = [path for path in paths.values() if path.exists()]
    if existing:
        raise RuntimeError(f"refusing to overwrite {existing}")
    received: list[MidiEvent] = []
    event_lock = threading.Lock()

    def receive_midi(event_data: tuple[list[int], float], _: Any = None) -> None:
        message, delta_seconds = event_data
        with event_lock:
            received.append(
                MidiEvent(
                    len(received) + 1,
                    time.monotonic_ns(),
                    delta_seconds,
                    tuple(message),
                )
            )

    midi_in.ignore_types(sysex=False, timing=False, active_sense=False)
    midi_in.set_callback(receive_midi)
    midi_in.open_port(input_index)
    midi_out.open_port(output_index)
    print(f"MIDI input: {input_ports[input_index]}", flush=True)
    print(f"MIDI output: {output_ports[output_index]}", flush=True)
    print(f"Serial: {serial_port}", flush=True)
    capture = TimedRunCapture(duration_ms, MIDI_BPM, MIDI_SWING)
    sent: list[SentMidiEvent] = []
    completed_stimulus = False
    timeout_at = time.monotonic() + duration_ms / 1000.0 + 30.0
    try:
        with serial.Serial(serial_port, 115200, timeout=0.25) as connection:
            connection.reset_input_buffer()
            connection.write(
                f"EXTERNAL_RUN {duration_ms} {MIDI_BPM} {MIDI_SWING}\n".encode("ascii")
            )
            connection.flush()
            _read_until(connection, capture, lambda: capture.started, timeout_at)
            sent = send_stimulus(midi_out, planned)
            completed_stimulus = True
            _read_until(connection, capture, lambda: capture.completed, timeout_at)
    finally:
        if not completed_stimulus:
            midi_out.send_message([0xFC])
        time.sleep(0.1)
        midi_in.close_port()
        midi_out.close_port()
    capture.require_complete()
    assert capture.diagnostics_row is not None
    assert capture.input_row is not None
    assert capture.runtime_row is not None
    diagnostics_version, diagnostics = parse_diagnostics_row(capture.diagnostics_row)
    if diagnostics_version != 4:
        raise RuntimeError(f"Stage 5.5.2 requires diagnostics v4, received v{diagnostics_version}")
    input_diagnostics = parse_input_diagnostics_row(capture.input_row)
    runtime_diagnostics = parse_runtime_diagnostics_row(capture.runtime_row)
    with event_lock:
        captured_events = list(received)
    host_metrics = analyze_events(captured_events, MIDI_BPM)
    acceptance = evaluate_acceptance(captured_events, sent, diagnostics)
    report = {
        "schema": "swing_metro_stage5_external_clock_report_v1",
        "run_id": RUN_NAME,
        "result": acceptance["result"],
        "requested_run": {
            "duration_ms": duration_ms,
            "bpm": MIDI_BPM,
            "swing": MIDI_SWING,
            "clock_mode": "external",
        },
        "protocol": {"control": "swing_metro_control_v1", "command": "EXTERNAL_RUN"},
        "metadata": dict(metadata),
        "ports": {
            "serial": serial_port,
            "midi_input": input_ports[input_index],
            "midi_output": output_ports[output_index],
        },
        "stimulus": {
            "clock_domain": "host_monotonic_ns",
            "metrics": stimulus_metrics(sent),
            "actual_timestamps_file": paths["stimulus"].name,
        },
        "host_midi": {
            "clock_domain": "host_monotonic_ns",
            "metrics": host_metrics,
            "events_file": paths["midi"].name,
        },
        "device_local": {
            "clock_domain": "pico_micros_uint32",
            "fresh_upload_before_capture": True,
            "diagnostics": {
                "schema": capture.diagnostics_row.split(",", 1)[0],
                "version": diagnostics_version,
                "scope": "boot_cumulative",
                "metrics": diagnostics,
            },
            "input": {
                "schema": capture.input_row.split(",", 1)[0],
                "scope": "boot_cumulative",
                "metrics": input_diagnostics,
            },
            "runtime": {
                "schema": capture.runtime_row.split(",", 1)[0],
                "scope": "timed_run_window",
                "metrics": runtime_diagnostics,
            },
        },
        "acceptance": acceptance,
        "limitations": [
            "Host send timestamps are not device-local MIDI interrupt latency measurements.",
            "Waiting/Locked/Lost is validated through transport behavior and device counters, "
            "not a sampled trace of every transient state.",
        ],
    }
    paths["diagnostics"].write_text(
        f"{diagnostics_header_for_row(capture.diagnostics_row)}\n{capture.diagnostics_row}\n",
        encoding="utf-8",
    )
    paths["input"].write_text(
        f"{input_diagnostics_header_for_row(capture.input_row)}\n{capture.input_row}\n",
        encoding="utf-8",
    )
    paths["runtime"].write_text(
        f"{runtime_diagnostics_header_for_row(capture.runtime_row)}\n{capture.runtime_row}\n",
        encoding="utf-8",
    )
    write_midi_csv(paths["midi"], captured_events)
    write_stimulus_csv(paths["stimulus"], sent)
    write_exact_json(paths["report"], report)
    print(f"Saved Stage 5.5.2 evidence under {output_dir}", flush=True)
    return report


def main() -> int:
    args = parse_args()
    if args.list_midi_ports:
        rtmidi = load_rtmidi()
        for kind, midi in (("input", rtmidi.MidiIn()), ("output", rtmidi.MidiOut())):
            for index, name in enumerate(midi.get_ports()):
                print(f"{kind} {index}: {name}")
        return 0
    if not args.pattern_confirmed:
        raise RuntimeError(
            "set External Clock, enable all 16 steps with Gate 100, then pass --pattern-confirmed"
        )
    if args.post_upload_settle_seconds < 0:
        raise RuntimeError("--post-upload-settle-seconds must not be negative")
    output_dir = args.output_dir or allocate_output_dir(args.output_root)
    planned = build_stimulus()
    subprocess.run([args.pio, "run", "--environment", PRODUCTION_ENVIRONMENT], check=True)
    if not PRODUCTION_ELF.is_file():
        raise RuntimeError(f"production firmware ELF is missing: {PRODUCTION_ELF}")
    revision, dirty = git_metadata()
    config = platformio_environment_config(args.pio, PRODUCTION_ENVIRONMENT)
    build_flags = config.get("build_flags")
    if not isinstance(build_flags, list) or not all(isinstance(flag, str) for flag in build_flags):
        raise RuntimeError("unable to identify PlatformIO build flags")
    toolchain, dependencies = platformio_packages(args.pio, PRODUCTION_ENVIRONMENT)
    binary_sha256 = sha256(PRODUCTION_ELF)
    metadata = _metadata(
        revision,
        dirty,
        binary_sha256,
        build_flags,
        toolchain,
        dependencies,
        args.usb_topology,
    )
    manifest = {
        "schema": "swing_metro_stage5_external_clock_manifest_v1",
        "run_id": RUN_NAME,
        "firmware_environment": PRODUCTION_ENVIRONMENT,
        "firmware_sha256": binary_sha256,
        "upload_before_run": True,
        "program": metadata["scenario"]["program"],
        "clock_mode": "external",
        "planned_stimulus": [
            {
                "sequence": event.sequence,
                "phase": event.phase,
                "scheduled_offset_ns": event.scheduled_offset_ns,
                "message_hex": " ".join(f"{byte:02X}" for byte in event.message),
            }
            for event in planned
        ],
    }
    write_exact_json(output_dir / f"{RUN_NAME}-metadata.json", metadata)
    write_exact_json(output_dir / f"{RUN_NAME}-manifest.json", manifest)
    subprocess.run(
        [args.pio, "run", "--environment", PRODUCTION_ENVIRONMENT, "--target", "upload"],
        check=True,
    )
    time.sleep(args.post_upload_settle_seconds)
    report = _capture(args, output_dir, metadata, planned)
    write_exact_json(
        output_dir / f"{RUN_NAME}-summary.json",
        {
            "schema": "swing_metro_stage5_external_clock_summary_v1",
            "run_id": RUN_NAME,
            "result": report["result"],
            "checks": report["acceptance"]["checks"],
        },
    )
    if report["result"] != "pass":
        failed = [
            name for name, passed in report["acceptance"]["checks"].items() if not passed
        ]
        raise RuntimeError(f"Stage 5.5.2 acceptance failed: {failed}")
    print(f"Stage 5.5.2 external Clock validation passed. Results: {output_dir}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
