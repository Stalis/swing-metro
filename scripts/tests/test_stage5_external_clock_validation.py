import csv
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from pico_midi_run import MidiEvent
from stage5_external_clock_validation import (
    LOSS_PAUSE_NS,
    MIDI_BPM,
    SentMidiEvent,
    build_stimulus,
    evaluate_acceptance,
    stimulus_duration_ms,
    stimulus_metrics,
    write_stimulus_csv,
)


def sent_stimulus() -> list[SentMidiEvent]:
    origin = 5_000_000_000
    return [
        SentMidiEvent(
            event.sequence,
            event.phase,
            event.scheduled_offset_ns,
            origin + event.scheduled_offset_ns,
            event.message,
        )
        for event in build_stimulus()
    ]


def passing_diagnostics(note_events: int) -> dict[str, int]:
    values = {
        "session_ends_external_clock_lost": 1,
        "session_ends_stop": 3,
        "explicit_clean_starts": 1,
        "session_generation_advances": 3,
        "delivery_clock_attempts": 0,
        "delivery_clock_accepted": 0,
        "outgoing_internal_f8_attempts": 0,
        "delivery_transport_attempts": 0,
        "delivery_transport_accepted": 0,
        "delivery_note_attempts": note_events,
        "delivery_note_accepted": note_events,
        "current_outbox_depth": 0,
    }
    for field in (
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
    ):
        values[field] = 0
    return values


class Stage5ExternalClockValidationTests(unittest.TestCase):
    def test_stimulus_has_fixed_transport_shape_and_bounded_zero_sum_jitter(self):
        events = build_stimulus()
        interval_ns = round(60_000_000_000 / (MIDI_BPM * 24))
        jittered = [event for event in events if event.phase == "jitter_clock"]
        jittered_intervals = [
            current.scheduled_offset_ns - previous.scheduled_offset_ns
            for previous, current in zip(jittered, jittered[1:])
        ]

        self.assertEqual(337, sum(event.message == (0xF8,) for event in events))
        self.assertEqual(1, sum(event.message == (0xFA,) for event in events))
        self.assertEqual(2, sum(event.message == (0xFB,) for event in events))
        self.assertEqual(2, sum(event.message == (0xFC,) for event in events))
        self.assertEqual(96, len(jittered))
        self.assertLessEqual(max(abs(value - interval_ns) for value in jittered_intervals), 1_000_000)
        self.assertEqual(
            interval_ns * 4,
            sum(jittered_intervals[:4]),
        )

    def test_stimulus_contains_one_loss_pause_and_finishes_before_capture(self):
        events = build_stimulus()
        last_resumed = max(
            event.scheduled_offset_ns for event in events if event.phase == "resumed_clock"
        )
        relock = next(
            event.scheduled_offset_ns for event in events if event.phase == "relock_clock"
        )

        self.assertEqual(LOSS_PAUSE_NS, relock - last_resumed)
        self.assertGreater(stimulus_duration_ms(events) * 1_000_000, events[-1].scheduled_offset_ns)
        self.assertEqual("final_stop", events[-1].phase)

    def test_stimulus_metrics_report_actual_loss_gap_and_send_shape(self):
        metrics = stimulus_metrics(sent_stimulus())

        self.assertEqual(342, metrics["event_count"])
        self.assertEqual(337, metrics["clock_count"])
        self.assertEqual(400_000.0, metrics["actual_loss_pause_us"])
        self.assertEqual(0.0, metrics["max_absolute_schedule_error_us"])

    def test_acceptance_passes_balanced_notes_before_and_after_relock(self):
        sent = sent_stimulus()
        pre_loss = next(event.host_send_ns for event in sent if event.phase == "start") + 10
        post_relock = next(
            event.host_send_ns for event in sent if event.phase == "continue_after_loss"
        ) + 10
        received = [
            MidiEvent(1, pre_loss, 0.0, (0x90, 60, 100)),
            MidiEvent(2, pre_loss + 1, 0.0, (0x80, 60, 0)),
            MidiEvent(3, post_relock, 0.0, (0x90, 61, 100)),
            MidiEvent(4, post_relock + 1, 0.0, (0x90, 61, 0)),
        ]

        acceptance = evaluate_acceptance(received, sent, passing_diagnostics(4))

        self.assertEqual("pass", acceptance["result"])
        self.assertTrue(all(acceptance["checks"].values()))

    def test_acceptance_rejects_realtime_echo_and_wrong_loss_count(self):
        sent = sent_stimulus()
        pre_loss = next(event.host_send_ns for event in sent if event.phase == "start") + 10
        post_relock = next(
            event.host_send_ns for event in sent if event.phase == "continue_after_loss"
        ) + 10
        received = [
            MidiEvent(1, pre_loss, 0.0, (0x90, 60, 100)),
            MidiEvent(2, pre_loss + 1, 0.0, (0x80, 60, 0)),
            MidiEvent(3, post_relock, 0.0, (0x90, 61, 100)),
            MidiEvent(4, post_relock + 1, 0.0, (0x80, 61, 0)),
            MidiEvent(5, post_relock + 2, 0.0, (0xF8,)),
        ]
        diagnostics = passing_diagnostics(4)
        diagnostics["session_ends_external_clock_lost"] = 2

        acceptance = evaluate_acceptance(received, sent, diagnostics)

        self.assertEqual("fail", acceptance["result"])
        self.assertFalse(acceptance["checks"]["no_realtime_echo"])
        self.assertFalse(acceptance["checks"]["external_loss_exactly_once"])

    def test_acceptance_rejects_missing_post_relock_notes_and_delivery_fault(self):
        sent = sent_stimulus()
        pre_loss = next(event.host_send_ns for event in sent if event.phase == "start") + 10
        received = [
            MidiEvent(1, pre_loss, 0.0, (0x90, 60, 100)),
            MidiEvent(2, pre_loss + 1, 0.0, (0x80, 60, 0)),
        ]
        diagnostics = passing_diagnostics(2)
        diagnostics["delivery_note_retry_later"] = 1

        acceptance = evaluate_acceptance(received, sent, diagnostics)

        self.assertEqual("fail", acceptance["result"])
        self.assertFalse(acceptance["checks"]["notes_after_relock"])
        self.assertFalse(acceptance["checks"]["delivery_and_queue_faults_zero"])

    def test_stimulus_csv_keeps_planned_and_actual_host_timestamps(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "stimulus.csv"
            events = [
                SentMidiEvent(1, "start", 100_000, 1_000_000, (0xFA,)),
                SentMidiEvent(2, "steady_clock", 200_000, 1_101_000, (0xF8,)),
            ]

            write_stimulus_csv(path, events)

            with path.open(encoding="utf-8") as source:
                rows = list(csv.DictReader(source))
            self.assertEqual("101.0", rows[1]["actual_offset_us"])
            self.assertEqual("1.0", rows[1]["schedule_error_us"])
            self.assertEqual("F8", rows[1]["message_hex"])


if __name__ == "__main__":
    unittest.main()
