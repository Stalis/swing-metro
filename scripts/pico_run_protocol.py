"""Shared host-side helpers for the Swing Metro timed-run protocol."""

from __future__ import annotations

import glob


CONTROL_PREFIX = "swing_metro_control_v1"
DIAGNOSTICS_PREFIX = "swing_metro_diagnostics_v2"
DIAGNOSTICS_COLUMNS = (
    DIAGNOSTICS_PREFIX,
    "alarm_callback_invocations",
    "synchronous_start_publication_attempts",
    "successful_publications",
    "failed_publications",
    "tick_queue_overflows",
    "stop_discards",
    "mode_switch_discards",
    "storage_discards",
    "alarm_arm_failures",
    "stale_alarm_callbacks",
    "stale_alarm_arm_failures",
    "missed_scheduled_targets",
    "out_of_horizon_alarm_callbacks",
    "max_actual_callback_interval_us",
    "max_callback_lateness_us",
    "successful_consumer_pops",
    "budget_discards",
    "outgoing_internal_f8_attempts",
    "max_service_interval_us",
    "max_internal_tick_processing_lateness_us",
    "max_external_tick_processing_lateness_us",
    "max_f8_attempt_lateness_us",
    "max_queued_event_attempt_lateness_us",
    "max_internal_ticks_popped_per_process_pass",
    "internal_tick_budget_reached_passes",
    "max_remaining_internal_ticks_after_budget_pass",
    "max_process_duration_us",
)
DIAGNOSTICS_HEADER = ",".join(DIAGNOSTICS_COLUMNS)


def find_serial_port(explicit_port: str | None) -> str:
    if explicit_port:
        return explicit_port
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) != 1:
        raise RuntimeError(f"expected one /dev/cu.usbmodem* port, found {ports}")
    return ports[0]


def parse_diagnostics_row(row: str) -> dict[str, int]:
    fields = row.split(",")
    if fields[0] != DIAGNOSTICS_PREFIX or len(fields) != len(DIAGNOSTICS_COLUMNS):
        raise RuntimeError(
            f"expected {len(DIAGNOSTICS_COLUMNS)} diagnostics fields, found {len(fields)}"
        )
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError as error:
        raise RuntimeError("diagnostics row contains a non-integer value") from error
    return dict(zip(DIAGNOSTICS_COLUMNS[1:], values, strict=True))
