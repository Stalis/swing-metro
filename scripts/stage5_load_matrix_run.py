#!/usr/bin/env python3
"""Prepare and run the Stage 5.4 hardware matrix as one reproducible workflow."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import pathlib
import platform
import re
import subprocess
import sys
from typing import Any

from stage5_load_matrix import FIRMWARE_ENVIRONMENT, matrix, run_name


PRODUCTION_ENVIRONMENT = "rpipico2"
DEFAULT_OUTPUT_ROOT = pathlib.Path("data/stage5-4-runs")
FAULT_ELF = pathlib.Path(".pio/build") / FIRMWARE_ENVIRONMENT / "firmware.elf"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", type=pathlib.Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--port", help="Serial port; auto-detected when omitted")
    parser.add_argument("--midi-port", help="MIDI input port index or unique name substring")
    parser.add_argument("--pio", default="pio", help="PlatformIO executable (default: pio)")
    parser.add_argument("--duration-seconds", type=float, default=244.0)
    parser.add_argument("--post-upload-settle-seconds", type=float, default=3.0)
    parser.add_argument("--usb-topology", default="direct USB connection; not independently verified")
    parser.add_argument("--resume", action="store_true")
    return parser.parse_args()


def allocate_output_dir(root: pathlib.Path, now: datetime.datetime | None = None) -> pathlib.Path:
    timestamp = (now or datetime.datetime.now()).strftime("%Y%m%d-%H%M%S")
    candidate = root / timestamp
    suffix = 2
    while candidate.exists():
        candidate = root / f"{timestamp}-{suffix:02d}"
        suffix += 1
    return candidate


def run_output(command: list[str]) -> str:
    return subprocess.run(command, check=True, text=True, stdout=subprocess.PIPE).stdout


def git_metadata() -> tuple[str, bool]:
    revision = run_output(["git", "rev-parse", "HEAD"]).strip()
    dirty = bool(run_output(["git", "status", "--porcelain"]).strip())
    return revision, dirty


def platformio_environment_config(
    pio: str, environment: str = FIRMWARE_ENVIRONMENT
) -> dict[str, Any]:
    sections = json.loads(run_output([pio, "project", "config", "--json-output"]))
    for section_name, options in sections:
        if section_name == f"env:{environment}":
            return dict(options)
    raise RuntimeError(f"PlatformIO environment is missing: {environment}")


def platformio_packages(
    pio: str, environment: str = FIRMWARE_ENVIRONMENT
) -> tuple[str, list[str]]:
    output = run_output([pio, "pkg", "list", "-e", environment])
    packages: list[str] = []
    toolchain = ""
    for line in output.splitlines():
        match = re.search(r"(?:├──|└──|\+--|`--)\s+(.+?)\s+@\s+([^ ]+)", line)
        if match is None:
            continue
        name, version = match.groups()
        value = f"{name} {version}"
        if name.startswith("toolchain-"):
            toolchain = value
        elif not name.startswith("tool-"):
            packages.append(value)
    if not toolchain:
        raise RuntimeError("unable to identify the PlatformIO toolchain")
    if not packages:
        raise RuntimeError("unable to identify PlatformIO dependencies")
    return toolchain, packages


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def metadata_value(
    name: str,
    fault_scenario: str | None,
    revision: str,
    dirty: bool,
    binary_sha256: str,
    build_flags: list[str],
    toolchain: str,
    dependencies: list[str],
    usb_topology: str,
) -> dict[str, Any]:
    value: dict[str, Any] = {
        "run_id": name,
        "scenario": {"id": name, "interaction": "none"},
        "instrumentation": {
            "stage5": "enabled",
            "fault_scenario": fault_scenario or "baseline",
        },
        "firmware": {
            "source_revision": revision,
            "working_tree_dirty": dirty,
            "binary_sha256": binary_sha256,
            "platformio_environment": FIRMWARE_ENVIRONMENT,
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
            "notes": "automated matrix; no operator interaction",
        },
    }
    if fault_scenario is not None:
        value["fault_injection"] = {"scenario": fault_scenario}
    return value


def write_metadata(
    directory: pathlib.Path,
    revision: str,
    dirty: bool,
    binary_sha256: str,
    build_flags: list[str],
    toolchain: str,
    dependencies: list[str],
    usb_topology: str,
) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    for bpm, swing, fault_scenario in matrix():
        name = run_name(bpm, swing, fault_scenario)
        path = directory / f"{name}-metadata.json"
        value = metadata_value(
            name,
            fault_scenario,
            revision,
            dirty,
            binary_sha256,
            build_flags,
            toolchain,
            dependencies,
            usb_topology,
        )
        encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
        if path.exists() and path.read_text(encoding="utf-8") != encoded:
            raise RuntimeError(f"refusing to replace mismatched metadata: {path}")
        path.write_text(encoded, encoding="utf-8")


def matrix_command(args: argparse.Namespace, output_dir: pathlib.Path) -> list[str]:
    command = [
        sys.executable,
        str(pathlib.Path(__file__).with_name("stage5_load_matrix.py")),
        "--output-dir",
        str(output_dir),
        "--metadata-dir",
        str(output_dir / "metadata"),
        "--pio",
        args.pio,
        "--duration-seconds",
        str(args.duration_seconds),
        "--post-upload-settle-seconds",
        str(args.post_upload_settle_seconds),
    ]
    if args.port:
        command.extend(("--port", args.port))
    if args.midi_port:
        command.extend(("--midi-port", args.midi_port))
    if args.resume:
        command.append("--resume")
    return command


def restore_production(pio: str) -> None:
    subprocess.run(
        [pio, "run", "--environment", PRODUCTION_ENVIRONMENT, "--target", "upload"],
        check=True,
    )


def run_matrix_and_restore(command: list[str], pio: str) -> None:
    primary_error: BaseException | None = None
    try:
        subprocess.run(command, check=True)
    except BaseException as error:
        primary_error = error
    try:
        print("Restoring production firmware...", flush=True)
        restore_production(pio)
    except (OSError, subprocess.CalledProcessError) as restore_error:
        if primary_error is not None:
            raise RuntimeError(
                f"matrix failed and production firmware restoration also failed: {restore_error}"
            ) from primary_error
        raise
    if primary_error is not None:
        raise primary_error


def main() -> int:
    args = parse_args()
    if args.duration_seconds <= 0:
        raise RuntimeError("--duration-seconds must be positive")
    if args.resume and args.output_dir is None:
        raise RuntimeError("--resume requires --output-dir")
    output_dir = args.output_dir or allocate_output_dir(args.output_root)
    metadata_dir = output_dir / "metadata"

    print(f"Building {FIRMWARE_ENVIRONMENT}...", flush=True)
    subprocess.run([args.pio, "run", "--environment", FIRMWARE_ENVIRONMENT], check=True)
    if not FAULT_ELF.is_file():
        raise RuntimeError(f"fault firmware ELF is missing: {FAULT_ELF}")

    revision, dirty = git_metadata()
    config = platformio_environment_config(args.pio)
    build_flags = config.get("build_flags")
    if not isinstance(build_flags, list) or not all(isinstance(flag, str) for flag in build_flags):
        raise RuntimeError("unable to identify PlatformIO build flags")
    toolchain, dependencies = platformio_packages(args.pio)
    write_metadata(
        metadata_dir,
        revision,
        dirty,
        sha256(FAULT_ELF),
        build_flags,
        toolchain,
        dependencies,
        args.usb_topology,
    )
    print(f"Results directory: {output_dir}", flush=True)
    run_matrix_and_restore(matrix_command(args, output_dir), args.pio)
    print(f"Stage 5.4 matrix passed. Results: {output_dir}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
