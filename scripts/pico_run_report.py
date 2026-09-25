"""Strict timed-run capture state and versioned host report helpers."""

from __future__ import annotations

import datetime
import json
import pathlib
import platform
import sys
from dataclasses import dataclass
from typing import Any, Mapping

from pico_run_protocol import (
    CONTROL_PREFIX,
    INPUT_DIAGNOSTICS_COLUMNS,
    INPUT_DIAGNOSTICS_PREFIX,
    RUNTIME_DIAGNOSTICS_COLUMNS,
    RUNTIME_DIAGNOSTICS_PREFIX,
    diagnostics_columns_for_row,
    parse_diagnostics_row,
    parse_input_diagnostics_row,
    parse_runtime_diagnostics_row,
)


REPORT_SCHEMA = "swing_metro_timed_run_report_v1"

_REQUIRED_METADATA: tuple[tuple[tuple[str, ...], type], ...] = (
    (("run_id",), str),
    (("scenario", "id"), str),
    (("firmware", "source_revision"), str),
    (("firmware", "binary_sha256"), str),
    (("firmware", "platformio_environment"), str),
    (("firmware", "build_flags"), list),
    (("firmware", "toolchain"), str),
    (("firmware", "dependencies"), list),
    (("instrumentation", "stage5"), str),
    (("hardware", "board"), str),
    (("hardware", "cpu_frequency_hz"), int),
    (("hardware", "usb_topology"), str),
)


def report_output_path(path: pathlib.Path) -> pathlib.Path:
    return path.with_name(f"{path.stem}-report.json")


def load_report_metadata(path: pathlib.Path | None) -> dict[str, Any]:
    if path is None:
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"unable to read metadata JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise RuntimeError("metadata JSON must contain one object")
    _validate_present_metadata(value)
    return value


def _nested_value(value: Mapping[str, Any], path: tuple[str, ...]) -> Any:
    current: Any = value
    for component in path:
        if not isinstance(current, Mapping) or component not in current:
            return None
        current = current[component]
    return current


def _validate_present_metadata(metadata: Mapping[str, Any]) -> None:
    for section in ("scenario", "firmware", "instrumentation", "hardware", "host"):
        if section in metadata and not isinstance(metadata[section], Mapping):
            raise RuntimeError(f"metadata field {section} must be an object")
    for path, expected_type in _REQUIRED_METADATA:
        value = _nested_value(metadata, path)
        if value is None:
            continue
        if not isinstance(value, expected_type) or isinstance(value, bool):
            raise RuntimeError(
                f"metadata field {'.'.join(path)} must be {expected_type.__name__}"
            )
        if expected_type is str and not value:
            raise RuntimeError(f"metadata field {'.'.join(path)} must not be empty")
    build_flags = _nested_value(metadata, ("firmware", "build_flags"))
    if build_flags is not None and not all(isinstance(value, str) for value in build_flags):
        raise RuntimeError("metadata field firmware.build_flags must contain only strings")
    dependencies = _nested_value(metadata, ("firmware", "dependencies"))
    if dependencies is not None and not all(isinstance(value, str) for value in dependencies):
        raise RuntimeError("metadata field firmware.dependencies must contain only strings")
    sha256 = _nested_value(metadata, ("firmware", "binary_sha256"))
    if sha256 is not None and (
        len(sha256) != 64
        or any(character not in "0123456789abcdefABCDEF" for character in sha256)
    ):
        raise RuntimeError("metadata field firmware.binary_sha256 must be 64 hexadecimal digits")
    instrumentation = _nested_value(metadata, ("instrumentation", "stage5"))
    if instrumentation is not None and instrumentation not in ("enabled", "disabled"):
        raise RuntimeError("metadata field instrumentation.stage5 must be enabled or disabled")
    frequency = _nested_value(metadata, ("hardware", "cpu_frequency_hz"))
    if frequency is not None and frequency <= 0:
        raise RuntimeError("metadata field hardware.cpu_frequency_hz must be positive")


def metadata_completeness(metadata: Mapping[str, Any]) -> tuple[bool, list[str]]:
    missing = [
        ".".join(path)
        for path, _ in _REQUIRED_METADATA
        if _nested_value(metadata, path) is None
    ]
    return not missing, missing


@dataclass
class TimedRunCapture:
    duration_ms: int
    bpm: int
    swing: int
    started: bool = False
    completed: bool = False
    diagnostics_row: str | None = None
    input_row: str | None = None
    runtime_row: str | None = None

    def consume(self, row: str) -> None:
        if row.startswith(f"{CONTROL_PREFIX},"):
            self._consume_control(row)
            return
        if row.startswith("swing_metro_diagnostics_"):
            _, columns = diagnostics_columns_for_row(row)
            if tuple(row.split(",")) == columns:
                return
            parse_diagnostics_row(row)
            self._record_row("diagnostics_row", row)
            return
        if row.startswith(f"{INPUT_DIAGNOSTICS_PREFIX},"):
            if tuple(row.split(",")) == INPUT_DIAGNOSTICS_COLUMNS:
                return
            parse_input_diagnostics_row(row)
            self._record_row("input_row", row)
            return
        if row.startswith(f"{RUNTIME_DIAGNOSTICS_PREFIX},"):
            if tuple(row.split(",")) == RUNTIME_DIAGNOSTICS_COLUMNS:
                return
            parse_runtime_diagnostics_row(row)
            self._record_row("runtime_row", row)

    def _consume_control(self, row: str) -> None:
        fields = row.split(",")
        if fields[1:2] == ["run_started"]:
            if len(fields) != 5:
                raise RuntimeError("invalid run_started field count")
            if self.started:
                raise RuntimeError("duplicate run_started row")
            if self.completed or any(
                value is not None
                for value in (self.diagnostics_row, self.input_row, self.runtime_row)
            ):
                raise RuntimeError("run_started row is out of order")
            try:
                observed = tuple(int(value) for value in fields[2:])
            except ValueError as error:
                raise RuntimeError("run_started contains a non-integer value") from error
            expected = (self.duration_ms, self.bpm, self.swing)
            if observed != expected:
                raise RuntimeError(f"run_started mismatch: expected {expected}, received {observed}")
            self.started = True
            return
        if fields[1:] == ["run_complete"]:
            if self.completed:
                raise RuntimeError("duplicate run_complete row")
            if not self.started:
                raise RuntimeError("run_complete arrived before run_started")
            missing = self.missing_rows()
            if missing:
                raise RuntimeError(f"run_complete arrived before {', '.join(missing)}")
            self.completed = True
            return
        if fields[1:2] == ["error"]:
            raise RuntimeError(f"device rejected timed run: {row}")
        raise RuntimeError(f"unrecognized control row: {row}")

    def _record_row(self, attribute: str, row: str) -> None:
        if not self.started:
            raise RuntimeError(f"{attribute} arrived before run_started")
        if self.completed:
            raise RuntimeError(f"{attribute} arrived after run_complete")
        if getattr(self, attribute) is not None:
            raise RuntimeError(f"duplicate {attribute}")
        setattr(self, attribute, row)

    def missing_rows(self) -> list[str]:
        return [
            name
            for name, value in (
                ("diagnostics", self.diagnostics_row),
                ("input diagnostics", self.input_row),
                ("runtime diagnostics", self.runtime_row),
            )
            if value is None
        ]

    def require_complete(self) -> None:
        if not self.completed:
            raise RuntimeError("timed run did not report a complete ordered capture")


def build_timed_run_report(
    *,
    capture: TimedRunCapture,
    metadata: Mapping[str, Any],
    serial_port: str,
    midi_port: str | None,
    host_midi: Mapping[str, int | float] | None,
) -> dict[str, Any]:
    capture.require_complete()
    assert capture.diagnostics_row is not None
    assert capture.input_row is not None
    assert capture.runtime_row is not None
    diagnostics_version, diagnostics = parse_diagnostics_row(capture.diagnostics_row)
    input_diagnostics = parse_input_diagnostics_row(capture.input_row)
    runtime_diagnostics = parse_runtime_diagnostics_row(capture.runtime_row)
    reproducibility_complete, missing = metadata_completeness(metadata)
    fresh_boot_candidate = diagnostics["synchronous_start_publication_attempts"] == 1
    if not reproducibility_complete:
        comparison_eligibility = "insufficient_metadata"
    elif not fresh_boot_candidate:
        comparison_eligibility = "boot_cumulative_not_run_comparable"
    else:
        comparison_eligibility = "paired_capture_candidate"
    diagnostics_schema = capture.diagnostics_row.split(",", 1)[0]
    report: dict[str, Any] = {
        "schema": REPORT_SCHEMA,
        "captured_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "run_id": metadata.get("run_id"),
        "requested_run": {
            "duration_ms": capture.duration_ms,
            "bpm": capture.bpm,
            "swing": capture.swing,
        },
        "protocol": {"control": CONTROL_PREFIX},
        "instrumentation": metadata.get("instrumentation", {}),
        "firmware": metadata.get("firmware", {}),
        "hardware": metadata.get("hardware", {}),
        "scenario": metadata.get("scenario", {}),
        "host": {
            **metadata.get("host", {}),
            "os": platform.platform(),
            "python": sys.version.split()[0],
            "serial_port": serial_port,
            "midi_port": midi_port,
        },
        "reproducibility_complete": reproducibility_complete,
        "missing_metadata_fields": missing,
        "comparison_eligibility": comparison_eligibility,
        "device_local": {
            "clock_domain": "pico_micros_uint32",
            "fresh_boot_candidate": fresh_boot_candidate,
            "diagnostics": {
                "schema": diagnostics_schema,
                "version": diagnostics_version,
                "scope": "boot_cumulative",
                "metrics": diagnostics,
            },
            "input": {
                "schema": INPUT_DIAGNOSTICS_PREFIX,
                "scope": "boot_cumulative",
                "metrics": input_diagnostics,
            },
            "runtime": {
                "schema": RUNTIME_DIAGNOSTICS_PREFIX,
                "scope": "timed_run_window",
                "metrics": runtime_diagnostics,
            },
        },
        "host_midi": (
            {
                "available": True,
                "clock_domain": "host_monotonic_ns",
                "metrics": dict(host_midi),
            }
            if host_midi is not None
            else {
                "available": False,
                "reason": "serial_only_capture",
            }
        ),
    }
    return report


def write_timed_run_report(path: pathlib.Path, report: Mapping[str, Any]) -> None:
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
