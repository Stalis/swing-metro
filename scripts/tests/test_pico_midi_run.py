import datetime
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))

from pico_midi_run import (
    MidiEvent,
    add_firmware_summary,
    analyze_events,
    choose_midi_port,
    event_name,
    input_diagnostics_output_path,
    output_paths,
    wait_for_midi_port,
    write_summary_csv,
)
from pico_serial_run import input_output_path
from stage5_load_matrix import (
    FIRMWARE_ENVIRONMENT,
    matrix,
    platformio_upload_command,
    upload_firmware,
    validate_metadata,
    validate_report,
)
from stage5_load_matrix_run import (
    allocate_output_dir,
    platformio_packages,
    run_matrix_and_restore,
    write_metadata,
)
from stage5_production_internal_matrix import (
    PATTERNS,
    SCENARIO_CELLS,
    run_name as production_run_name,
    validate_report as validate_production_report,
)
from pico_run_report import (
    FAULT_REPORT_SCHEMA,
    REPORT_SCHEMA,
    TimedRunCapture,
    build_timed_run_report,
    load_report_metadata,
    metadata_completeness,
    report_output_path,
    validate_fault_metadata,
)
from pico_run_protocol import (
    V4_COLUMNS,
    RUNTIME_DIAGNOSTICS_COLUMNS,
    diagnostics_header_for_row,
    is_diagnostics_data_row,
    is_input_diagnostics_data_row,
    is_runtime_diagnostics_data_row,
    input_diagnostics_header_for_row,
    parse_input_diagnostics_row,
    parse_runtime_diagnostics_row,
    parse_diagnostics_row,
    runtime_diagnostics_header_for_row,
    wait_for_serial_port,
)


class PicoMidiRunTests(unittest.TestCase):
    @staticmethod
    def complete_capture(attempts: int = 1) -> TimedRunCapture:
        capture = TimedRunCapture(1_000, 120, 50)
        capture.consume("swing_metro_control_v1,run_started,1000,120,50")
        capture.consume(",".join(V4_COLUMNS))
        values = [0 for _ in V4_COLUMNS[1:]]
        values[V4_COLUMNS.index("synchronous_start_publication_attempts") - 1] = attempts
        capture.consume(",".join([V4_COLUMNS[0], *(str(value) for value in values)]))
        capture.consume("swing_metro_input_diagnostics_v1,1250,2")
        capture.consume(
            ",".join([RUNTIME_DIAGNOSTICS_COLUMNS[0], *("0" for _ in RUNTIME_DIAGNOSTICS_COLUMNS[1:])])
        )
        capture.consume("swing_metro_control_v1,run_complete")
        return capture

    def test_selects_unique_named_midi_port(self):
        ports = ["Other Device", "Swing Metro MIDI"]
        self.assertEqual(1, choose_midi_port(ports, None))
        self.assertEqual(1, choose_midi_port(ports, "metro"))
        self.assertEqual(0, choose_midi_port(ports, "0"))

    def test_rejects_ambiguous_midi_port(self):
        with self.assertRaises(RuntimeError):
            choose_midi_port(["Pico A", "Pico B"], None)

    def test_waits_for_midi_port_to_reappear_after_upload(self):
        midi_in = unittest.mock.Mock()
        midi_in.get_ports.side_effect = [[], [], ["Pico 2W"]]
        with patch("pico_midi_run.time.sleep") as sleep:
            index, ports = wait_for_midi_port(midi_in, "Pico 2W", attempts=3)
        self.assertEqual(0, index)
        self.assertEqual(["Pico 2W"], ports)
        self.assertEqual(2, sleep.call_count)

    def test_midi_port_wait_remains_bounded(self):
        midi_in = unittest.mock.Mock()
        midi_in.get_ports.return_value = []
        with patch("pico_midi_run.time.sleep") as sleep:
            with self.assertRaisesRegex(RuntimeError, "matched"):
                wait_for_midi_port(midi_in, "Pico 2W", attempts=2)
        self.assertEqual(1, sleep.call_count)

    def test_waits_for_serial_port_to_reappear_after_upload(self):
        with patch("pico_run_protocol.pathlib.Path.exists", side_effect=[False, False, True]):
            with patch("pico_run_protocol.time.sleep") as sleep:
                port = wait_for_serial_port("/dev/cu.usbmodem101", attempts=3)
        self.assertEqual("/dev/cu.usbmodem101", port)
        self.assertEqual(2, sleep.call_count)

    def test_names_realtime_and_channel_messages(self):
        self.assertEqual("Clock", event_name((0xF8,)))
        self.assertEqual("Start", event_name((0xFA,)))
        self.assertEqual("Stop", event_name((0xFC,)))
        self.assertEqual("Note On", event_name((0x90, 60, 100)))
        self.assertEqual("Note Off", event_name((0x90, 60, 0)))

    def test_analyzes_clock_gaps(self):
        events = [
            MidiEvent(1, 0, 0.0, (0xFA,)),
            MidiEvent(2, 1_000_000, 0.0, (0xF8,)),
            MidiEvent(3, 26_000_000, 0.025, (0xF8,)),
            MidiEvent(4, 76_000_000, 0.050, (0xF8,)),
            MidiEvent(5, 77_000_000, 0.001, (0xFC,)),
        ]
        summary = analyze_events(events, 100)
        self.assertEqual(3, summary["clock_count"])
        self.assertEqual(1, summary["long_interval_count"])
        self.assertEqual(0, summary["short_interval_count"])
        self.assertEqual(1, summary["estimated_missing_clock_count"])
        self.assertEqual(50_000.0, summary["max_clock_interval_us"])

    def test_analyzes_absolute_clock_jitter_and_drift(self):
        events = [
            MidiEvent(1, 1_000_000, 0.0, (0xF8,)),
            MidiEvent(2, 27_000_000, 0.026, (0xF8,)),
            MidiEvent(3, 49_000_000, 0.022, (0xF8,)),
            MidiEvent(4, 74_000_000, 0.025, (0xF8,)),
        ]

        summary = analyze_events(events, 100)

        self.assertEqual(2_800.0, summary["host_abs_jitter_p95_us"])
        self.assertEqual(2_960.0, summary["host_abs_jitter_p99_us"])
        self.assertEqual(2_000.0, summary["absolute_clock_drift_us"])

    def test_analyzes_balanced_and_dangling_notes(self):
        events = [
            MidiEvent(1, 1_000_000, 0.0, (0x90, 60, 100)),
            MidiEvent(2, 2_000_000, 0.0, (0x90, 60, 0)),
            MidiEvent(3, 3_000_000, 0.0, (0x91, 61, 100)),
            MidiEvent(4, 4_000_000, 0.0, (0x81, 62, 0)),
        ]

        summary = analyze_events(events, 120)

        self.assertEqual(2, summary["note_on_count"])
        self.assertEqual(2, summary["note_off_count"])
        self.assertEqual(1, summary["unmatched_note_off_count"])
        self.assertEqual(1, summary["dangling_note_on_count"])
        self.assertEqual(1, summary["max_active_note_count"])

    def test_clock_jitter_and_drift_are_zero_with_fewer_than_two_clocks(self):
        summary = analyze_events([MidiEvent(1, 1_000_000, 0.0, (0xF8,))], 100)

        self.assertEqual(0.0, summary["host_abs_jitter_p95_us"])
        self.assertEqual(0.0, summary["host_abs_jitter_p99_us"])
        self.assertEqual(0.0, summary["absolute_clock_drift_us"])

    def test_output_paths_append_distinct_suffixes(self):
        self.assertEqual(
            (
                pathlib.Path("capture-minder-midi.csv"),
                pathlib.Path("capture-minder-diagnostics.csv"),
                pathlib.Path("capture-minder-summary.csv"),
                pathlib.Path("capture-minder-runtime-diagnostics.csv"),
            ),
            output_paths(pathlib.Path("capture-minder")),
        )
        self.assertEqual(
            pathlib.Path("capture-minder-input-diagnostics.csv"),
            input_diagnostics_output_path(pathlib.Path("capture-minder")),
        )
        self.assertEqual(
            pathlib.Path("capture-input.csv"), input_output_path(pathlib.Path("capture.csv"))
        )
        self.assertEqual(
            pathlib.Path("capture-minder-report.json"),
            report_output_path(pathlib.Path("capture-minder")),
        )

    def test_timed_run_capture_requires_exact_order_cardinality_and_parameters(self):
        capture = self.complete_capture()
        capture.require_complete()
        with self.assertRaisesRegex(RuntimeError, "duplicate run_complete"):
            capture.consume("swing_metro_control_v1,run_complete")

        with self.assertRaisesRegex(RuntimeError, "mismatch"):
            TimedRunCapture(1_000, 120, 50).consume(
                "swing_metro_control_v1,run_started,1000,121,50"
            )
        with self.assertRaisesRegex(RuntimeError, "before run_started"):
            TimedRunCapture(1_000, 120, 50).consume(
                "swing_metro_input_diagnostics_v1,1250,2"
            )

        duplicate = TimedRunCapture(1_000, 120, 50)
        duplicate.consume("swing_metro_control_v1,run_started,1000,120,50")
        diagnostics_row = ",".join([V4_COLUMNS[0], *("0" for _ in V4_COLUMNS[1:])])
        duplicate.consume(diagnostics_row)
        with self.assertRaisesRegex(RuntimeError, "duplicate diagnostics_row"):
            duplicate.consume(diagnostics_row)

        incomplete = TimedRunCapture(1_000, 120, 50)
        incomplete.consume("swing_metro_control_v1,run_started,1000,120,50")
        with self.assertRaisesRegex(RuntimeError, "run_complete arrived before"):
            incomplete.consume("swing_metro_control_v1,run_complete")

    def test_fault_capture_requires_one_matching_ack_before_run(self):
        capture = TimedRunCapture(1_000, 120, 50, "retry_first_clock")
        with self.assertRaisesRegex(RuntimeError, "before fault acknowledgement"):
            capture.consume("swing_metro_control_v1,run_started,1000,120,50")
        capture.consume("swing_metro_fault_v1,selected,retry_first_clock")
        with self.assertRaisesRegex(RuntimeError, "duplicate fault acknowledgement"):
            capture.consume("swing_metro_fault_v1,selected,retry_first_clock")

        mismatch = TimedRunCapture(1_000, 120, 50, "retry_first_clock")
        with self.assertRaisesRegex(RuntimeError, "mismatch"):
            mismatch.consume("swing_metro_fault_v1,selected,baseline")

    def test_stage5_matrix_is_bounded_and_has_one_run_per_fault(self):
        cells = matrix()
        self.assertEqual(9, len(cells))
        self.assertEqual(
            ((40, 50, None), (40, 90, None), (120, 50, None), (120, 90, None),
             (240, 50, None), (240, 90, None)),
            cells[:6],
        )
        self.assertEqual(
            ((120, 50, "retry_first_clock"), (120, 50, "sustained_backpressure"),
             (120, 50, "deterministic_disconnect")),
            cells[6:],
        )

    def test_fault_metadata_must_match_cli_without_reusing_study_id(self):
        metadata = {
            "scenario": {"id": "study-42"},
            "fault_injection": {"scenario": "retry_first_clock"},
        }
        validate_fault_metadata(metadata, "retry_first_clock")
        with self.assertRaisesRegex(RuntimeError, "normal run"):
            validate_fault_metadata(metadata, None)
        with self.assertRaisesRegex(RuntimeError, "mismatch"):
            validate_fault_metadata(metadata, "deterministic_disconnect")
        with self.assertRaisesRegex(RuntimeError, "requires"):
            validate_fault_metadata({"scenario": {"id": "study-42"}}, "retry_first_clock")
        with self.assertRaisesRegex(RuntimeError, "known"):
            validate_fault_metadata(
                {"fault_injection": {"scenario": "not-a-fault"}}, "retry_first_clock"
            )

    def test_baseline_matrix_predicates_reject_empty_or_mismatched_midi_capture(self):
        report = {
            "comparison_eligibility": "paired_capture_candidate",
            "device_local": {
                "fresh_boot_candidate": True,
                "diagnostics": {
                    "metrics": {
                        "delivery_clock_accepted": 4,
                        "outgoing_internal_f8_attempts": 4,
                        "failed_publications": 0,
                        "tick_queue_overflows": 0,
                        "internal_tick_queue_overflows": 0,
                    }
                }
            },
            "host_midi": {
                "available": True,
                "metrics": {
                    "start_count": 1,
                    "stop_count": 1,
                    "clock_count": 4,
                    "long_interval_count": 0,
                    "short_interval_count": 0,
                    "estimated_missing_clock_count": 0,
                },
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "baseline-report.json"
            path.write_text(json.dumps(report), encoding="utf-8")
            validate_report(path, None)
            report["host_midi"]["metrics"]["clock_count"] = 0
            path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "clock_count"):
                validate_report(path, None)
            report["host_midi"]["metrics"]["clock_count"] = 3
            path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "does not equal"):
                validate_report(path, None)

            report["host_midi"]["metrics"]["clock_count"] = 4
            report["host_midi"]["metrics"]["long_interval_count"] = 1
            report["host_midi"]["metrics"]["short_interval_count"] = 1
            report["host_midi"]["metrics"]["estimated_missing_clock_count"] = 1
            path.write_text(json.dumps(report), encoding="utf-8")
            validate_report(path, None)

    def test_matrix_requires_fresh_boot_and_paired_capture_eligibility(self):
        report = {
            "comparison_eligibility": "paired_capture_candidate",
            "device_local": {"fresh_boot_candidate": True},
        }
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "report.json"
            path.write_text(json.dumps(report), encoding="utf-8")
            report["device_local"]["fresh_boot_candidate"] = False
            path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "fresh boot"):
                validate_report(path, "retry_first_clock")
            report["device_local"]["fresh_boot_candidate"] = True
            report["comparison_eligibility"] = "boot_cumulative_not_run_comparable"
            path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "eligible"):
                validate_report(path, "retry_first_clock")

    def test_matrix_upload_is_fixed_to_fault_environment_without_shell(self):
        command = ["custom-pio", "run", "--environment", FIRMWARE_ENVIRONMENT, "--target", "upload"]
        self.assertEqual(command, platformio_upload_command("custom-pio"))
        with patch("stage5_load_matrix.subprocess.run") as run:
            with patch("stage5_load_matrix.time.sleep") as sleep:
                upload_firmware("custom-pio")
        run.assert_called_once_with(command, check=True)
        sleep.assert_called_once_with(3.0)

    def test_matrix_metadata_requires_fault_firmware_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "metadata.json"
            path.write_text(json.dumps({"firmware": {"platformio_environment": "rpipico2"}}))
            with self.assertRaisesRegex(RuntimeError, FIRMWARE_ENVIRONMENT):
                validate_metadata(path, None)

    def test_stage5_runner_allocates_unique_timestamped_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            now = datetime.datetime(2026, 9, 26, 12, 0, 0)
            first = root / "20260926-120000"
            first.mkdir()
            self.assertEqual(root / "20260926-120000-02", allocate_output_dir(root, now))

    def test_stage5_runner_extracts_toolchain_and_dependencies(self):
        output = """\
├── framework-arduinopico @ 1.60000.0
├── tool-picotool @ 5.0.0
└── toolchain-rp2040 @ 16.1.0
Libraries
└── lvgl @ 9.6.0
"""
        with patch("stage5_load_matrix_run.run_output", return_value=output):
            toolchain, dependencies = platformio_packages("custom-pio")
        self.assertEqual("toolchain-rp2040 16.1.0", toolchain)
        self.assertEqual(["framework-arduinopico 1.60000.0", "lvgl 9.6.0"], dependencies)

    def test_stage5_runner_writes_complete_metadata_for_all_cells(self):
        with tempfile.TemporaryDirectory() as directory:
            metadata_dir = pathlib.Path(directory)
            write_metadata(
                metadata_dir,
                "abc123",
                False,
                "a" * 64,
                ["-DUSE_TINYUSB", "-DSWING_METRO_STAGE5_FAULT_SCENARIOS=1"],
                "toolchain 16.1.0",
                ["lvgl 9.6.0"],
                "direct",
            )
            paths = sorted(metadata_dir.glob("*.json"))
            self.assertEqual(9, len(paths))
            for path in paths:
                value = load_report_metadata(path)
                self.assertEqual((True, []), metadata_completeness(value))
            retry = json.loads(
                (metadata_dir / "stage5-4-retry_first_clock-120bpm-swing50-metadata.json")
                .read_text(encoding="utf-8")
            )
            self.assertEqual("retry_first_clock", retry["fault_injection"]["scenario"])

    def test_stage5_runner_restores_production_after_matrix_failure(self):
        failure = subprocess.CalledProcessError(1, ["matrix"])
        with patch("stage5_load_matrix_run.subprocess.run", side_effect=[failure, None]) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                run_matrix_and_restore(["matrix"], "custom-pio")
        self.assertEqual(
            ["custom-pio", "run", "--environment", "rpipico2", "--target", "upload"],
            run.call_args_list[1].args[0],
        )

    def test_stage5_production_matrix_is_bounded_and_names_patterns(self):
        self.assertEqual(((68, 50),), SCENARIO_CELLS["gate100"])
        self.assertEqual(4, len(SCENARIO_CELLS["mixed-gate"]))
        self.assertEqual(16, len(PATTERNS["mixed-gate"]["gate_percent_by_step"]))
        self.assertEqual(
            "stage5-5-internal-mixed-gate-240bpm-swing90",
            production_run_name("mixed-gate", 240, 90),
        )

    def test_stage5_production_predicate_requires_balanced_host_and_device_notes(self):
        diagnostics = {
            "delivery_clock_accepted": 4,
            "outgoing_internal_f8_attempts": 4,
            "delivery_note_attempts": 2,
            "delivery_note_accepted": 2,
            "delivery_clock_retry_later": 0,
            "delivery_clock_disconnected": 0,
            "delivery_note_retry_later": 0,
            "delivery_note_disconnected": 0,
            "terminal_note_off_abandoned_count": 0,
            "note_on_expired_count": 0,
            "failed_publications": 0,
            "tick_queue_overflows": 0,
            "internal_tick_queue_overflows": 0,
        }
        report = {
            "schema": "swing_metro_timed_run_report_v1",
            "comparison_eligibility": "paired_capture_candidate",
            "device_local": {
                "fresh_boot_candidate": True,
                "diagnostics": {"metrics": diagnostics},
            },
            "host_midi": {
                "metrics": {
                    "start_count": 1,
                    "stop_count": 1,
                    "clock_count": 4,
                    "note_on_count": 1,
                    "note_off_count": 1,
                    "unmatched_note_off_count": 0,
                    "dangling_note_on_count": 0,
                }
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "report.json"
            path.write_text(json.dumps(report), encoding="utf-8")
            validate_production_report(path)
            report["host_midi"]["metrics"]["dangling_note_on_count"] = 1
            path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "lifecycle"):
                validate_production_report(path)

    def test_metadata_validation_and_completeness_are_explicit(self):
        complete = {
            "run_id": "stage5-3-on-r01",
            "scenario": {"id": "baseline"},
            "firmware": {
                "source_revision": "abc123",
                "binary_sha256": "a" * 64,
                "platformio_environment": "rpipico2",
                "build_flags": ["-DUSE_TINYUSB"],
                "toolchain": "arm-none-eabi 16.1.0",
                "dependencies": ["lvgl 9.6.0", "GFX Library for Arduino 1.6.8"],
            },
            "instrumentation": {"stage5": "enabled"},
            "hardware": {
                "board": "Raspberry Pi Pico 2 W",
                "cpu_frequency_hz": 150_000_000,
                "usb_topology": "direct",
            },
        }
        self.assertEqual((True, []), metadata_completeness(complete))
        self.assertFalse(metadata_completeness({})[0])
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "metadata.json"
            path.write_text(json.dumps(complete), encoding="utf-8")
            self.assertEqual(complete, load_report_metadata(path))
            path.write_text('{"firmware":{"binary_sha256":"bad"}}', encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "64 hexadecimal"):
                load_report_metadata(path)

    def test_report_separates_clock_domains_and_never_overstates_eligibility(self):
        capture = self.complete_capture()
        report = build_timed_run_report(
            capture=capture,
            metadata={},
            serial_port="/dev/test",
            midi_port="Swing Metro MIDI",
            host_midi={"clock_count": 48},
        )
        self.assertEqual(REPORT_SCHEMA, report["schema"])
        self.assertEqual("pico_micros_uint32", report["device_local"]["clock_domain"])
        self.assertEqual("host_monotonic_ns", report["host_midi"]["clock_domain"])
        self.assertFalse(report["reproducibility_complete"])
        self.assertEqual("insufficient_metadata", report["comparison_eligibility"])

        serial_report = build_timed_run_report(
            capture=self.complete_capture(attempts=2),
            metadata={},
            serial_port="/dev/test",
            midi_port=None,
            host_midi=None,
        )
        self.assertFalse(serial_report["host_midi"]["available"])

        complete_metadata = {
            "run_id": "stage5-3-on-r01",
            "scenario": {"id": "baseline"},
            "firmware": {
                "source_revision": "abc123",
                "binary_sha256": "a" * 64,
                "platformio_environment": "rpipico2",
                "build_flags": ["-DUSE_TINYUSB"],
                "toolchain": "arm-none-eabi 16.1.0",
                "dependencies": ["lvgl 9.6.0", "GFX Library for Arduino 1.6.8"],
            },
            "instrumentation": {"stage5": "enabled"},
            "hardware": {
                "board": "Raspberry Pi Pico 2 W",
                "cpu_frequency_hz": 150_000_000,
                "usb_topology": "direct",
            },
        }
        comparable = build_timed_run_report(
            capture=self.complete_capture(),
            metadata=complete_metadata,
            serial_port="/dev/test",
            midi_port=None,
            host_midi=None,
        )
        self.assertEqual("paired_capture_candidate", comparable["comparison_eligibility"])
        cumulative = build_timed_run_report(
            capture=self.complete_capture(attempts=2),
            metadata=complete_metadata,
            serial_port="/dev/test",
            midi_port=None,
            host_midi=None,
        )
        self.assertEqual(
            "boot_cumulative_not_run_comparable", cumulative["comparison_eligibility"]
        )

        fault_capture = TimedRunCapture(1_000, 120, 50, "retry_first_clock")
        fault_capture.consume("swing_metro_fault_v1,selected,retry_first_clock")
        fault_capture.consume("swing_metro_control_v1,run_started,1000,120,50")
        fault_capture.consume(",".join([V4_COLUMNS[0], *("0" for _ in V4_COLUMNS[1:])]))
        fault_capture.consume("swing_metro_input_diagnostics_v1,1250,2")
        fault_capture.consume(",".join([RUNTIME_DIAGNOSTICS_COLUMNS[0], *("0" for _ in RUNTIME_DIAGNOSTICS_COLUMNS[1:])]))
        fault_capture.consume("swing_metro_control_v1,run_complete")
        fault_report = build_timed_run_report(
            capture=fault_capture, metadata={}, serial_port="/dev/test", midi_port=None, host_midi=None
        )
        self.assertEqual(FAULT_REPORT_SCHEMA, fault_report["schema"])
        self.assertEqual("retry_first_clock", fault_report["fault_injection"]["scenario"])
        self.assertTrue(fault_report["fault_injection"]["test_firmware_required"])

    def test_live_diagnostics_rejects_legacy_and_unknown_versions(self):
        # Independent historical sizes: 27 data fields in v2, 163 in v3.
        for prefix, count in (("swing_metro_diagnostics_v2", 27),
                              ("swing_metro_diagnostics_v3", 163),
                              ("swing_metro_diagnostics_v9", len(V4_COLUMNS) - 1)):
            for fields in (count, len(V4_COLUMNS) - 1):
                row = ",".join([prefix, *("0" for _ in range(fields))])
                with self.subTest(prefix=prefix, fields=fields):
                    with self.assertRaisesRegex(RuntimeError, "unsupported live diagnostics"):
                        parse_diagnostics_row(row)
                    with self.assertRaisesRegex(RuntimeError, "unsupported live diagnostics"):
                        diagnostics_header_for_row(row)
                    self.assertFalse(is_diagnostics_data_row(row))
                    with self.assertRaisesRegex(RuntimeError, "unsupported live diagnostics"):
                        TimedRunCapture(1000, 120, 50).consume(row)

    def test_current_diagnostics_rejects_wrong_length_and_noninteger(self):
        for fields in (len(V4_COLUMNS) - 2, len(V4_COLUMNS)):
            row = ",".join([V4_COLUMNS[0], *("0" for _ in range(fields))])
            with self.subTest(fields=fields), self.assertRaisesRegex(RuntimeError, "field count"):
                parse_diagnostics_row(row)
        row = ",".join([V4_COLUMNS[0], "invalid", *("0" for _ in V4_COLUMNS[2:])])
        with self.assertRaises(RuntimeError):
            parse_diagnostics_row(row)
        with self.assertRaises(RuntimeError):
            TimedRunCapture(1000, 120, 50).consume(row)
        self.assertFalse(is_diagnostics_data_row(",".join(V4_COLUMNS)))

    def test_current_diagnostics_preserves_header_and_delivery_fields(self):
        row = ",".join([V4_COLUMNS[0], *(str(index) for index in range(1, len(V4_COLUMNS)))])
        version, parsed = parse_diagnostics_row(row)
        self.assertEqual(4, version)
        self.assertEqual(",".join(V4_COLUMNS), diagnostics_header_for_row(row))
        self.assertTrue(is_diagnostics_data_row(row))
        self.assertEqual(28, parsed["delivery_clock_attempts"])
        self.assertEqual(8, len([column for column in V4_COLUMNS if column.startswith("session_ends_")]))
        self.assertIn("pending_removed_stale_gate_off_note", parsed)
        self.assertIn("scheduled_removed_stale_gate_off_note", parsed)
        self.assertEqual(V4_COLUMNS.index("clock_attempt_lateness_1_10"), parsed["clock_attempt_lateness_1_10"])
        self.assertIn("observed_internal_tick_queue_high_water", parsed)

    def test_parses_strict_input_diagnostics_row(self):
        row = "swing_metro_input_diagnostics_v1,1250,2"

        parsed = parse_input_diagnostics_row(row)
        self.assertEqual(1250, parsed["max_actual_encoder_sample_interval_us"])
        self.assertEqual(2, parsed["encoder_sample_intervals_above_1250_us"])
        self.assertEqual(
            "swing_metro_input_diagnostics_v1,max_actual_encoder_sample_interval_us,encoder_sample_intervals_above_1250_us",
            input_diagnostics_header_for_row(row),
        )
        self.assertTrue(is_input_diagnostics_data_row(row))
        self.assertFalse(is_input_diagnostics_data_row("swing_metro_input_diagnostics_v1,1250"))
        self.assertFalse(is_input_diagnostics_data_row("swing_metro_input_diagnostics_v1,one,2"))
        with self.assertRaises(RuntimeError):
            parse_input_diagnostics_row("swing_metro_input_diagnostics_v1,1250")
        with self.assertRaises(RuntimeError):
            parse_input_diagnostics_row("swing_metro_input_diagnostics_v1,one,2")

    def test_parses_strict_runtime_diagnostics_row(self):
        row = ",".join(
            [
                RUNTIME_DIAGNOSTICS_COLUMNS[0],
                *(str(index) for index in range(1, len(RUNTIME_DIAGNOSTICS_COLUMNS))),
            ]
        )

        parsed = parse_runtime_diagnostics_row(row)
        self.assertEqual(1, parsed["lv_timer_handler_count"])
        self.assertEqual(6, parsed["display_flush_inclusive_max_us"])
        self.assertEqual(",".join(RUNTIME_DIAGNOSTICS_COLUMNS), runtime_diagnostics_header_for_row(row))
        self.assertTrue(is_runtime_diagnostics_data_row(row))
        self.assertFalse(
            is_runtime_diagnostics_data_row(
                ",".join([RUNTIME_DIAGNOSTICS_COLUMNS[0], "1"])
            )
        )
        self.assertFalse(
            is_runtime_diagnostics_data_row(
                ",".join([RUNTIME_DIAGNOSTICS_COLUMNS[0], *("x" for _ in range(8))])
            )
        )
        with self.assertRaises(RuntimeError):
            parse_runtime_diagnostics_row(",".join([RUNTIME_DIAGNOSTICS_COLUMNS[0], "1"]))

    def test_summary_supports_non_comparable_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "summary.csv"
            write_summary_csv(path, {"host_clock_count_difference": "not_comparable"})
            self.assertIn("host_clock_count_difference,not_comparable", path.read_text())

    def test_current_summary_preserves_fresh_and_cumulative_comparisons(self):
        summary = {"clock_count": 55}
        diagnostics = {
            "synchronous_start_publication_attempts": 1,
            "outgoing_internal_f8_attempts": 60,
            "delivery_clock_attempts": 61,
            "delivery_clock_accepted": 58,
            "successful_publications": 60,
            "successful_consumer_pops": 60,
            "clock_attempt_lateness_1_10": 7,
            "note_accepted_lateness_101_250": 8,
            "current_scheduled_depth_total": 2,
            "max_scheduled_depth": 5,
            "observed_internal_tick_queue_depth": 1,
            "observed_internal_tick_queue_high_water": 4,
            "internal_tick_queue_overflows": 0,
        }
        combined = add_firmware_summary(summary, diagnostics)
        self.assertEqual(61, combined["firmware_clock_attempt_count"])
        self.assertEqual(58, combined["firmware_clock_stack_accepted_count"])
        self.assertEqual(55, combined["host_clock_count"])
        self.assertEqual(5, combined["host_clock_count_difference"])
        self.assertEqual(3, combined["clock_attempt_minus_accepted"])
        self.assertEqual(3, combined["clock_accepted_minus_host"])
        self.assertEqual(1, combined["firmware_counters_fresh_for_run"])
        self.assertEqual(7, combined["firmware_clock_attempt_lateness_1_10"])
        self.assertEqual(8, combined["firmware_note_accepted_lateness_101_250"])
        self.assertEqual(5, combined["firmware_max_scheduled_depth"])
        self.assertEqual(4, combined["firmware_observed_internal_tick_queue_high_water"])
        diagnostics["synchronous_start_publication_attempts"] = 2
        combined = add_firmware_summary(summary, diagnostics, 4)
        self.assertEqual(0, combined["firmware_counters_fresh_for_run"])
        for field in ("host_clock_count_difference", "clock_attempt_minus_accepted", "clock_accepted_minus_host"):
            self.assertEqual("not_comparable_cumulative_firmware_counters", combined[field])

    def test_current_summary_rejects_legacy_before_reading_metrics(self):
        for version in (2, 3, 9):
            with self.subTest(version=version), self.assertRaisesRegex(RuntimeError, "unsupported diagnostics version"):
                add_firmware_summary({}, {}, version)


if __name__ == "__main__":
    unittest.main()
