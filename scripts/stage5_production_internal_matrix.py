#!/usr/bin/env python3
"""Capture and validate the Stage 5.5.1 production internal/Gate matrix."""

from __future__ import annotations

import argparse
import json
import pathlib
import platform
import subprocess
import sys
import time
from typing import Any

from stage5_load_matrix import output_files
from stage5_load_matrix_run import (
    allocate_output_dir,
    git_metadata,
    platformio_environment_config,
    platformio_packages,
    sha256,
)


PRODUCTION_ENVIRONMENT = "rpipico2"
PRODUCTION_ELF = pathlib.Path(".pio/build/rpipico2/firmware.elf")
DEFAULT_OUTPUT_ROOT = pathlib.Path("data/stage5-5-internal-runs")
MANIFEST_NAME = "stage5-5-internal-manifest.json"
SUMMARY_NAME = "stage5-5-internal-summary.json"
SCENARIO_CELLS = {
    "gate100": ((68, 50),),
    "mixed-gate": ((68, 50), (68, 90), (240, 50), (240, 90)),
}
PATTERNS = {
    "gate100": {
        "steps": "all 16 enabled",
        "gate_percent_by_step": [100] * 16,
        "operator_interaction_during_capture": "none",
    },
    "mixed-gate": {
        "steps": "all 16 enabled",
        "gate_percent_by_step": [1, 25, 50, 75, 100, 1, 25, 50, 75, 100, 1, 25, 50, 75, 100, 1],
        "operator_interaction_during_capture": "none",
    },
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=tuple(SCENARIO_CELLS), required=True)
    parser.add_argument("--pattern-confirmed", action="store_true")
    parser.add_argument("--output-root", type=pathlib.Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--port", help="Serial port; auto-detected when omitted")
    parser.add_argument("--midi-port", help="MIDI input port index or unique name substring")
    parser.add_argument("--pio", default="pio")
    parser.add_argument("--duration-seconds", type=float, default=244.0)
    parser.add_argument("--post-upload-settle-seconds", type=float, default=3.0)
    parser.add_argument("--usb-topology", default="direct USB connection; not independently verified")
    parser.add_argument("--resume", action="store_true")
    return parser.parse_args()


def run_name(scenario: str, bpm: int, swing: int) -> str:
    return f"stage5-5-internal-{scenario}-{bpm}bpm-swing{swing}"


def upload_production(pio: str, settle_seconds: float) -> None:
    subprocess.run(
        [pio, "run", "--environment", PRODUCTION_ENVIRONMENT, "--target", "upload"],
        check=True,
    )
    time.sleep(settle_seconds)


def metadata_value(
    name: str,
    scenario: str,
    revision: str,
    dirty: bool,
    binary_sha256: str,
    build_flags: list[str],
    toolchain: str,
    dependencies: list[str],
    usb_topology: str,
) -> dict[str, Any]:
    return {
        "run_id": name,
        "scenario": {
            "id": name,
            "interaction": "none",
            "production_internal_profile": scenario,
            "program": PATTERNS[scenario],
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
            "notes": "automated production internal/Gate matrix; no interaction during capture",
        },
    }


def write_exact_json(path: pathlib.Path, value: Any) -> None:
    encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if path.exists() and path.read_text(encoding="utf-8") != encoded:
        raise RuntimeError(f"refusing to replace mismatched file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(encoded, encoding="utf-8")


def validate_report(path: pathlib.Path) -> dict[str, Any]:
    report = json.loads(path.read_text(encoding="utf-8"))
    if report.get("schema") != "swing_metro_timed_run_report_v1":
        raise RuntimeError(f"unexpected report schema: {path}")
    if report.get("comparison_eligibility") != "paired_capture_candidate":
        raise RuntimeError(f"report is not eligible for comparison: {path}")
    device = report["device_local"]
    if device.get("fresh_boot_candidate") is not True:
        raise RuntimeError(f"report is not from a fresh boot: {path}")
    diagnostics = device["diagnostics"]["metrics"]
    host = report["host_midi"]["metrics"]
    for field, expected in (("start_count", 1), ("stop_count", 1)):
        if host[field] != expected:
            raise RuntimeError(f"production predicate failed: {path}: {field}={host[field]}")
    if host["clock_count"] <= 0:
        raise RuntimeError(f"production predicate failed: {path}: no host Clock")
    for field in ("delivery_clock_accepted", "outgoing_internal_f8_attempts"):
        if diagnostics[field] != host["clock_count"]:
            raise RuntimeError(f"production predicate failed: {path}: Clock mismatch with {field}")
    if host["note_on_count"] <= 0 or host["note_on_count"] != host["note_off_count"]:
        raise RuntimeError(f"production predicate failed: {path}: unbalanced Note On/Off")
    if host["unmatched_note_off_count"] != 0 or host["dangling_note_on_count"] != 0:
        raise RuntimeError(f"production predicate failed: {path}: invalid host note lifecycle")
    host_note_events = host["note_on_count"] + host["note_off_count"]
    if diagnostics["delivery_note_accepted"] != host_note_events:
        raise RuntimeError(f"production predicate failed: {path}: device/host note mismatch")
    zero_fields = (
        "delivery_clock_retry_later",
        "delivery_clock_disconnected",
        "delivery_note_retry_later",
        "delivery_note_disconnected",
        "terminal_note_off_abandoned_count",
        "note_on_expired_count",
        "failed_publications",
        "tick_queue_overflows",
        "internal_tick_queue_overflows",
    )
    for field in zero_fields:
        if diagnostics[field] != 0:
            raise RuntimeError(f"production predicate failed: {path}: {field}={diagnostics[field]}")
    if diagnostics["delivery_note_attempts"] != diagnostics["delivery_note_accepted"]:
        raise RuntimeError(f"production predicate failed: {path}: note delivery attempts differ")
    return report


def summary_row(report: dict[str, Any]) -> dict[str, Any]:
    host = report["host_midi"]["metrics"]
    diagnostics = report["device_local"]["diagnostics"]["metrics"]
    runtime = report["device_local"]["runtime"]["metrics"]
    input_metrics = report["device_local"]["input"]["metrics"]
    return {
        "run_id": report["run_id"],
        "clock_count": host["clock_count"],
        "note_on_count": host["note_on_count"],
        "note_off_count": host["note_off_count"],
        "long_interval_count": host["long_interval_count"],
        "short_interval_count": host["short_interval_count"],
        "absolute_clock_drift_us": host["absolute_clock_drift_us"],
        "device_clock_max_lateness_us": diagnostics["delivery_clock_max_first_attempt_lateness_us"],
        "device_note_max_lateness_us": diagnostics["delivery_note_max_first_attempt_lateness_us"],
        "max_scheduled_depth": diagnostics["max_scheduled_depth"],
        "max_service_interval_us": diagnostics["max_service_interval_us"],
        "max_encoder_sample_interval_us": input_metrics["max_actual_encoder_sample_interval_us"],
        "lv_timer_handler_inclusive_max_us": runtime["lv_timer_handler_inclusive_max_us"],
        "display_flush_inclusive_max_us": runtime["display_flush_inclusive_max_us"],
    }


def main() -> int:
    args = parse_args()
    if not args.pattern_confirmed:
        raise RuntimeError(
            "confirm the documented device pattern, then pass --pattern-confirmed"
        )
    if args.duration_seconds <= 0:
        raise RuntimeError("--duration-seconds must be positive")
    if args.post_upload_settle_seconds < 0:
        raise RuntimeError("--post-upload-settle-seconds must not be negative")
    if args.resume and args.output_dir is None:
        raise RuntimeError("--resume requires --output-dir")

    output_dir = args.output_dir or allocate_output_dir(args.output_root)
    metadata_dir = output_dir / "metadata"
    cells = SCENARIO_CELLS[args.scenario]
    entries = [
        (bpm, swing, output_dir / run_name(args.scenario, bpm, swing))
        for bpm, swing in cells
    ]

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
    for _, _, prefix in entries:
        write_exact_json(
            metadata_dir / f"{prefix.name}-metadata.json",
            metadata_value(
                prefix.name,
                args.scenario,
                revision,
                dirty,
                binary_sha256,
                build_flags,
                toolchain,
                dependencies,
                args.usb_topology,
            ),
        )

    manifest_value = {
        "schema": "swing_metro_stage5_production_internal_matrix_v1",
        "scenario": args.scenario,
        "program": PATTERNS[args.scenario],
        "duration_seconds": args.duration_seconds,
        "serial_port_selector": args.port,
        "midi_port_selector": args.midi_port,
        "firmware_environment": PRODUCTION_ENVIRONMENT,
        "firmware_sha256": binary_sha256,
        "upload_before_each_run": True,
        "post_upload_settle_seconds": args.post_upload_settle_seconds,
        "runs": [
            {
                "bpm": bpm,
                "swing": swing,
                "output_prefix": str(prefix),
                "metadata": str(metadata_dir / f"{prefix.name}-metadata.json"),
            }
            for bpm, swing, prefix in entries
        ],
    }
    manifest = output_dir / MANIFEST_NAME
    completed: set[pathlib.Path] = set()
    if args.resume:
        if not manifest.exists() or json.loads(manifest.read_text(encoding="utf-8")) != manifest_value:
            raise RuntimeError("resume requires an exactly matching existing manifest")
        for _, _, prefix in entries:
            present = [path.exists() for path in output_files(prefix)]
            if any(present) and not all(present):
                raise RuntimeError(f"resume found a partial capture: {prefix}")
            if all(present):
                validate_report(prefix.with_name(f"{prefix.name}-report.json"))
                completed.add(prefix)
    else:
        existing = [
            path for _, _, prefix in entries for path in output_files(prefix) if path.exists()
        ]
        existing.extend(path for path in (manifest, output_dir / SUMMARY_NAME) if path.exists())
        if existing:
            raise RuntimeError(f"refusing to overwrite {existing}")
        write_exact_json(manifest, manifest_value)

    reports: list[dict[str, Any]] = []
    print(f"Results directory: {output_dir}", flush=True)
    for bpm, swing, prefix in entries:
        report_path = prefix.with_name(f"{prefix.name}-report.json")
        if prefix not in completed:
            upload_production(args.pio, args.post_upload_settle_seconds)
            command = [
                sys.executable,
                str(pathlib.Path(__file__).with_name("pico_midi_run.py")),
                "--duration-seconds",
                str(args.duration_seconds),
                "--bpm",
                str(bpm),
                "--swing",
                str(swing),
                "--metadata",
                str(metadata_dir / f"{prefix.name}-metadata.json"),
                "--output-prefix",
                str(prefix),
            ]
            if args.port:
                command.extend(("--port", args.port))
            if args.midi_port:
                command.extend(("--midi-port", args.midi_port))
            subprocess.run(command, check=True)
        reports.append(validate_report(report_path))

    summary = {
        "schema": "swing_metro_stage5_production_internal_summary_v1",
        "scenario": args.scenario,
        "result": "pass",
        "runs": [summary_row(report) for report in reports],
    }
    write_exact_json(output_dir / SUMMARY_NAME, summary)
    print(f"Stage 5.5.1 {args.scenario} matrix passed. Results: {output_dir}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
