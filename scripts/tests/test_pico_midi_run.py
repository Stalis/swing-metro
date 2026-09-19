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
    output_paths,
    write_summary_csv,
)
from pico_run_protocol import DIAGNOSTICS_HEADER, parse_diagnostics_row


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

    def test_output_paths_append_distinct_suffixes(self):
        self.assertEqual(
            (
                pathlib.Path("capture-minder-midi.csv"),
                pathlib.Path("capture-minder-diagnostics.csv"),
                pathlib.Path("capture-minder-summary.csv"),
            ),
            output_paths(pathlib.Path("capture-minder")),
        )

    def test_parses_full_diagnostics_row(self):
        columns = DIAGNOSTICS_HEADER.split(",")
        row = ",".join([columns[0], *(str(index) for index in range(1, len(columns)))])
        parsed = parse_diagnostics_row(row)
        self.assertEqual(1, parsed["alarm_callback_invocations"])
        self.assertEqual(27, parsed["max_process_duration_us"])

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


if __name__ == "__main__":
    unittest.main()
