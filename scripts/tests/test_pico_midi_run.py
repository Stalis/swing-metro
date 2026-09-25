import pathlib
import sys
import tempfile
import unittest


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
    write_summary_csv,
)
from pico_serial_run import input_output_path
from pico_run_protocol import (
    V2_COLUMNS,
    V3_COLUMNS,
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
)


class PicoMidiRunTests(unittest.TestCase):
    def test_selects_unique_named_midi_port(self):
        ports = ["Other Device", "Swing Metro MIDI"]
        self.assertEqual(1, choose_midi_port(ports, None))
        self.assertEqual(1, choose_midi_port(ports, "metro"))
        self.assertEqual(0, choose_midi_port(ports, "0"))

    def test_rejects_ambiguous_midi_port(self):
        with self.assertRaises(RuntimeError):
            choose_midi_port(["Pico A", "Pico B"], None)

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

    def test_parses_v2_diagnostics_row(self):
        columns = V2_COLUMNS
        row = ",".join([columns[0], *(str(index) for index in range(1, len(columns)))])
        version, parsed = parse_diagnostics_row(row)
        self.assertEqual(2, version)
        self.assertEqual(1, parsed["alarm_callback_invocations"])
        self.assertEqual(27, parsed["max_process_duration_us"])
        with self.assertRaises(RuntimeError):
            parse_diagnostics_row(",".join([columns[0], *("1" for _ in columns[2:])]))
        with self.assertRaises(RuntimeError):
            parse_diagnostics_row(",".join(["swing_metro_diagnostics_v9", *("1" for _ in columns[1:])]))

    def test_parses_v3_diagnostics_row_and_matches_header(self):
        row = ",".join([V3_COLUMNS[0], *(str(index) for index in range(1, len(V3_COLUMNS)))])
        version, parsed = parse_diagnostics_row(row)
        self.assertEqual(3, version)
        self.assertEqual(len(V2_COLUMNS), V3_COLUMNS.index("delivery_clock_attempts"))
        self.assertEqual(len(V2_COLUMNS), parsed["delivery_clock_attempts"])
        self.assertEqual(8, len([column for column in V3_COLUMNS if column.startswith("session_ends_")]))
        self.assertEqual(",".join(V3_COLUMNS), diagnostics_header_for_row(row))
        self.assertTrue(is_diagnostics_data_row(row))
        self.assertIn("pending_removed_stale_gate_off_note", parsed)
        self.assertIn("scheduled_removed_stale_gate_off_note", parsed)
        self.assertFalse(is_diagnostics_data_row(",".join(V3_COLUMNS)))
        with self.assertRaises(RuntimeError):
            parse_diagnostics_row(",".join([V3_COLUMNS[0], *("1" for _ in V2_COLUMNS[1:])]))

    def test_parses_strict_v4_diagnostics_row_without_changing_v2_or_v3(self):
        row = ",".join([V4_COLUMNS[0], *(str(index) for index in range(1, len(V4_COLUMNS)))])
        version, parsed = parse_diagnostics_row(row)
        self.assertEqual(4, version)
        self.assertEqual(",".join(V4_COLUMNS), diagnostics_header_for_row(row))
        self.assertEqual(
            V4_COLUMNS.index("clock_attempt_lateness_1_10"),
            parsed["clock_attempt_lateness_1_10"],
        )
        self.assertIn("observed_internal_tick_queue_high_water", parsed)
        with self.assertRaises(RuntimeError):
            parse_diagnostics_row(",".join([V4_COLUMNS[0], *("1" for _ in V4_COLUMNS[2:])]))

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

    def test_firmware_comparison_requires_fresh_boot(self):
        summary = {"clock_count": 55}
        diagnostics = {
            "synchronous_start_publication_attempts": 1,
            "outgoing_internal_f8_attempts": 55,
            "successful_publications": 55,
            "successful_consumer_pops": 55,
        }
        combined = add_firmware_summary(summary, diagnostics)
        self.assertEqual(1, combined["firmware_counters_fresh_for_run"])
        self.assertEqual(0, combined["host_clock_count_difference"])

        diagnostics["synchronous_start_publication_attempts"] = 2
        combined = add_firmware_summary(summary, diagnostics)
        self.assertEqual(0, combined["firmware_counters_fresh_for_run"])
        self.assertEqual(
            "not_comparable_cumulative_firmware_counters",
            combined["host_clock_count_difference"],
        )

    def test_v3_firmware_summary_distinguishes_attempt_acceptance_and_host(self):
        summary = {"clock_count": 55}
        diagnostics = {
            "synchronous_start_publication_attempts": 1,
            "outgoing_internal_f8_attempts": 60,
            "delivery_clock_attempts": 61,
            "delivery_clock_accepted": 58,
            "successful_publications": 60,
            "successful_consumer_pops": 60,
        }
        combined = add_firmware_summary(summary, diagnostics, 3)
        self.assertEqual(61, combined["firmware_clock_attempt_count"])
        self.assertEqual(58, combined["firmware_clock_stack_accepted_count"])
        self.assertEqual(55, combined["host_clock_count"])
        self.assertEqual(3, combined["clock_attempt_minus_accepted"])
        self.assertEqual(3, combined["clock_accepted_minus_host"])
        self.assertEqual(1, combined["firmware_counters_fresh_for_run"])

        diagnostics["synchronous_start_publication_attempts"] = 2
        combined = add_firmware_summary(summary, diagnostics, 3)
        self.assertEqual(0, combined["firmware_counters_fresh_for_run"])
        self.assertEqual(
            "not_comparable_cumulative_firmware_counters",
            combined["clock_attempt_minus_accepted"],
        )
        self.assertEqual(
            "not_comparable_cumulative_firmware_counters",
            combined["clock_accepted_minus_host"],
        )

        diagnostics.update(
            {
                "clock_attempt_lateness_1_10": 7,
                "note_accepted_lateness_101_250": 8,
                "current_scheduled_depth_total": 2,
                "max_scheduled_depth": 5,
                "observed_internal_tick_queue_depth": 1,
                "observed_internal_tick_queue_high_water": 4,
                "internal_tick_queue_overflows": 0,
            }
        )
        combined = add_firmware_summary(summary, diagnostics, 4)
        self.assertEqual(58, combined["firmware_clock_stack_accepted_count"])
        self.assertEqual(7, combined["firmware_clock_attempt_lateness_1_10"])
        self.assertEqual(8, combined["firmware_note_accepted_lateness_101_250"])
        self.assertEqual(5, combined["firmware_max_scheduled_depth"])
        self.assertEqual(4, combined["firmware_observed_internal_tick_queue_high_water"])

    def test_v2_firmware_summary_marks_acceptance_unavailable(self):
        combined = add_firmware_summary(
            {"clock_count": 55},
            {
                "synchronous_start_publication_attempts": 1,
                "outgoing_internal_f8_attempts": 55,
                "successful_publications": 55,
                "successful_consumer_pops": 55,
            },
        )
        self.assertEqual("unavailable_v2", combined["firmware_clock_stack_accepted_count"])
        self.assertEqual("unavailable_v2", combined["clock_attempt_minus_accepted"])
        self.assertEqual("unavailable_v2", combined["clock_accepted_minus_host"])


if __name__ == "__main__":
    unittest.main()
