#!/usr/bin/env python3
"""Run the bounded Stage 5.4 baseline and deterministic fault capture matrix."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

from pico_run_report import validate_fault_metadata


BASELINE_CELLS = tuple((bpm, swing) for bpm in (40, 120, 240) for swing in (50, 90))
FAULT_SCENARIOS = ("retry_first_clock", "sustained_backpressure", "deterministic_disconnect")
MANIFEST_NAME = "stage5-4-load-matrix-manifest.json"
FIRMWARE_ENVIRONMENT = "rpipico2-stage5-fault-scenarios"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--metadata-dir", type=pathlib.Path, required=True)
    parser.add_argument("--port")
    parser.add_argument("--midi-port")
    parser.add_argument("--pio", default="pio", help="PlatformIO executable (default: pio)")
    parser.add_argument("--duration-seconds", type=float, default=244.0)
    return parser.parse_args()


def run_name(bpm: int, swing: int, fault_scenario: str | None) -> str:
    scenario = fault_scenario or "baseline"
    return f"stage5-4-{scenario}-{bpm}bpm-swing{swing}"


def output_files(prefix: pathlib.Path) -> tuple[pathlib.Path, ...]:
    report = prefix.with_name(f"{prefix.name}-report.json")
    return (
        pathlib.Path(f"{prefix}-midi.csv"),
        pathlib.Path(f"{prefix}-diagnostics.csv"),
        pathlib.Path(f"{prefix}-summary.csv"),
        pathlib.Path(f"{prefix}-input-diagnostics.csv"),
        pathlib.Path(f"{prefix}-runtime-diagnostics.csv"),
        report,
    )


def matrix() -> tuple[tuple[int, int, str | None], ...]:
    return tuple((bpm, swing, None) for bpm, swing in BASELINE_CELLS) + tuple(
        (120, 50, scenario) for scenario in FAULT_SCENARIOS
    )


def validate_metadata(path: pathlib.Path, fault_scenario: str | None) -> None:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"metadata must be an object: {path}")
    validate_fault_metadata(value, fault_scenario)
    firmware = value.get("firmware")
    if not isinstance(firmware, dict) or firmware.get("platformio_environment") != FIRMWARE_ENVIRONMENT:
        raise RuntimeError(f"metadata must name {FIRMWARE_ENVIRONMENT}: {path}")


def platformio_upload_command(pio: str) -> list[str]:
    return [pio, "run", "--environment", FIRMWARE_ENVIRONMENT, "--target", "upload"]


def upload_firmware(pio: str) -> None:
    subprocess.run(platformio_upload_command(pio), check=True)


def validate_report(path: pathlib.Path, fault_scenario: str | None) -> None:
    report = json.loads(path.read_text(encoding="utf-8"))
    device_local = report.get("device_local", {})
    if device_local.get("fresh_boot_candidate") is not True:
        raise RuntimeError(f"report is not from a fresh boot: {path}")
    if report.get("comparison_eligibility") != "paired_capture_candidate":
        raise RuntimeError(f"report is not eligible for paired capture comparison: {path}")
    if fault_scenario is None:
        diagnostics = device_local["diagnostics"]["metrics"]
        if not report["host_midi"]["available"]:
            raise RuntimeError(f"baseline predicate failed: {path}: host MIDI is unavailable")
        host = report["host_midi"]["metrics"]
        for field, value in (("start_count", 1), ("stop_count", 1)):
            if host[field] != value:
                raise RuntimeError(f"baseline predicate failed: {path}: {field}={host[field]}")
        if host["clock_count"] <= 0:
            raise RuntimeError(f"baseline predicate failed: {path}: clock_count={host['clock_count']}")
        for field in ("delivery_clock_accepted", "outgoing_internal_f8_attempts"):
            if host["clock_count"] != diagnostics[field]:
                raise RuntimeError(
                    f"baseline predicate failed: {path}: host clock count does not equal {field}"
                )
        for field in ("long_interval_count", "short_interval_count", "estimated_missing_clock_count"):
            if host[field] != 0:
                raise RuntimeError(f"baseline predicate failed: {path}: {field}={host[field]}")
        for field in ("failed_publications", "tick_queue_overflows", "internal_tick_queue_overflows"):
            if diagnostics[field] != 0:
                raise RuntimeError(f"baseline predicate failed: {path}: {field}={diagnostics[field]}")
        return
    fault = report.get("fault_injection")
    if report.get("schema") != "swing_metro_fault_run_report_v1" or fault is None:
        raise RuntimeError(f"fault report schema missing: {path}")
    if fault.get("scenario") != fault_scenario or not fault.get("acknowledged"):
        raise RuntimeError(f"fault report acknowledgement mismatch: {path}")
    diagnostics = device_local["diagnostics"]["metrics"]
    expected = {
        "retry_first_clock": {"delivery_clock_retry_later": 1, "delivery_clock_retry_recovered": 1},
        "sustained_backpressure": {"retry_window_safety_stops": 1},
        "deterministic_disconnect": {"delivery_clock_disconnected": 1},
    }[fault_scenario]
    for field, value in expected.items():
        if diagnostics[field] != value:
            raise RuntimeError(f"fault predicate failed: {path}: {field}={diagnostics[field]}")


def main() -> int:
    args = parse_args()
    entries = []
    for bpm, swing, fault_scenario in matrix():
        name = run_name(bpm, swing, fault_scenario)
        prefix = args.output_dir / name
        metadata = args.metadata_dir / f"{name}-metadata.json"
        if not metadata.is_file():
            raise RuntimeError(f"missing metadata: {metadata}")
        validate_metadata(metadata, fault_scenario)
        entries.append((bpm, swing, fault_scenario, prefix, metadata))
    manifest = args.output_dir / MANIFEST_NAME
    existing = [
        path
        for _, _, _, prefix, _ in entries
        for path in output_files(prefix)
        if path.exists()
    ]
    if manifest.exists():
        existing.append(manifest)
    if existing:
        raise RuntimeError(f"refusing to overwrite {existing}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest.write_text(
        json.dumps(
            {
                "schema": "swing_metro_stage5_load_matrix_v1",
                "duration_seconds": args.duration_seconds,
                "serial_port_selector": args.port,
                "midi_port_selector": args.midi_port,
                "firmware_environment": FIRMWARE_ENVIRONMENT,
                "upload_before_each_run": True,
                "runs": [
                    {
                        "bpm": bpm,
                        "swing": swing,
                        "fault_scenario": fault_scenario,
                        "output_prefix": str(prefix),
                        "metadata": str(metadata),
                    }
                    for bpm, swing, fault_scenario, prefix, metadata in entries
                ],
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    for bpm, swing, fault_scenario, prefix, metadata in entries:
        upload_firmware(args.pio)
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
            str(metadata),
            "--output-prefix",
            str(prefix),
        ]
        if args.port:
            command.extend(("--port", args.port))
        if args.midi_port:
            command.extend(("--midi-port", args.midi_port))
        if fault_scenario:
            command.extend(("--fault-scenario", fault_scenario))
        subprocess.run(command, check=True)
        validate_report(prefix.with_name(f"{prefix.name}-report.json"), fault_scenario)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
