"""Frozen R0 references: never regenerate these fixtures from the code under test."""

import datetime
import hashlib
import json
import pathlib
import sys
import unittest
from unittest.mock import patch


SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))

from pico_run_protocol import (
    diagnostics_header_for_row,
    input_diagnostics_header_for_row,
    parse_diagnostics_row,
    parse_input_diagnostics_row,
    parse_runtime_diagnostics_row,
    runtime_diagnostics_header_for_row,
)
from pico_run_report import TimedRunCapture, build_timed_run_report


FIXTURES = pathlib.Path(__file__).parent / "fixtures" / "refactor_baseline_v4"


class RefactorBaselineTests(unittest.TestCase):
    def test_historical_fixture_bytes_match_recorded_provenance(self):
        provenance = json.loads((FIXTURES / "provenance.json").read_text())
        for entry in provenance["files"]:
            with self.subTest(file=entry["fixture"]):
                digest = hashlib.sha256((FIXTURES / entry["fixture"]).read_bytes()).hexdigest()
                self.assertEqual(entry["sha256"], digest)

    def test_current_wire_headers_and_metrics_match_historical_capture(self):
        expected = json.loads((FIXTURES / "report.json").read_text())
        for filename, section, header_for_row, parse in (
            ("diagnostics.csv", "diagnostics", diagnostics_header_for_row, parse_diagnostics_row),
            ("input-diagnostics.csv", "input", input_diagnostics_header_for_row, parse_input_diagnostics_row),
            ("runtime-diagnostics.csv", "runtime", runtime_diagnostics_header_for_row, parse_runtime_diagnostics_row),
        ):
            with self.subTest(section=section):
                header, row = (FIXTURES / filename).read_text().splitlines()
                self.assertEqual(header, header_for_row(row))
                parsed = parse(row)
                if section == "diagnostics":
                    version, parsed = parsed
                    self.assertEqual(4, version)
                self.assertEqual(expected["device_local"][section]["metrics"], parsed)

    def test_current_report_replays_frozen_capture_without_semantic_drift(self):
        expected = json.loads((FIXTURES / "report.json").read_text())
        metadata = json.loads((FIXTURES / "metadata.json").read_text())
        run = expected["requested_run"]
        capture = TimedRunCapture(run["duration_ms"], run["bpm"], run["swing"])
        # The historical artifact has CSV rows, not a raw serial transcript.
        # Reconstruct only the framing; the measured rows remain byte-for-byte fixtures.
        capture.consume(
            f"swing_metro_control_v1,run_started,{run['duration_ms']},{run['bpm']},{run['swing']}"
        )
        for filename in ("diagnostics.csv", "input-diagnostics.csv", "runtime-diagnostics.csv"):
            for row in (FIXTURES / filename).read_text().splitlines():
                capture.consume(row)
        capture.consume("swing_metro_control_v1,run_complete")

        # Pin only environment/time fields. All report semantics and measured metrics
        # must equal the original report, including its dirty-firmware disclosure.
        captured_at = datetime.datetime.fromisoformat(expected["captured_at_utc"])
        with (
            patch("pico_run_report.datetime.datetime") as clock,
            patch("pico_run_report.platform.platform", return_value=expected["host"]["os"]),
            patch("pico_run_report.sys.version", expected["host"]["python"]),
        ):
            clock.now.return_value = captured_at
            actual = build_timed_run_report(
                capture=capture,
                metadata=metadata,
                serial_port=expected["host"]["serial_port"],
                midi_port=expected["host"]["midi_port"],
                host_midi=expected["host_midi"]["metrics"],
            )
        self.assertEqual(expected, actual)
