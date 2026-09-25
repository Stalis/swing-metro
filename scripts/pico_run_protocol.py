"""Shared host-side helpers for the Swing Metro timed-run protocol."""

from __future__ import annotations

import glob


CONTROL_PREFIX = "swing_metro_control_v1"
V2_DIAGNOSTICS_PREFIX = "swing_metro_diagnostics_v2"
V3_DIAGNOSTICS_PREFIX = "swing_metro_diagnostics_v3"
INPUT_DIAGNOSTICS_PREFIX = "swing_metro_input_diagnostics_v1"
RUNTIME_DIAGNOSTICS_PREFIX = "swing_metro_runtime_diagnostics_v1"
V2_COLUMNS = (
    V2_DIAGNOSTICS_PREFIX,
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
_DELIVERY_CLASSES = ("clock", "transport", "note")
_DELIVERY_FIELDS = (
    "attempts",
    "accepted",
    "retry_later",
    "disconnected",
    "retry_recovered",
    "max_first_attempt_lateness_us",
    "max_acceptance_lateness_us",
)
_INVALIDATION_REASONS = (
    "clock_coalesced",
    "clock_expired",
    "note_on_expired",
    "stop",
    "mode_switch",
    "storage",
    "external_clock_lost",
    "retry_window_exceeded",
    "delivery_capacity",
    "disconnected",
    "superseded_start",
    "scheduled_overdue",
    "stale_gate_off",
)
_SESSION_END_REASONS = (
    "stop",
    "mode_switch",
    "storage",
    "external_clock_lost",
    "retry_window_exceeded",
    "delivery_capacity",
    "disconnected",
    "superseded_start",
)
V3_COLUMNS = (
    V3_DIAGNOSTICS_PREFIX,
    *V2_COLUMNS[1:],
    *(f"delivery_{message_class}_{field}" for message_class in _DELIVERY_CLASSES for field in _DELIVERY_FIELDS),
    *(f"{queue}_removed_{reason}_{message_class}" for reason in _INVALIDATION_REASONS for message_class in _DELIVERY_CLASSES for queue in ("pending", "scheduled")),
    *(f"{field}_{message_class}" for message_class in _DELIVERY_CLASSES for field in ("scheduled_created", "scheduled_transferred", "outbox_inserted", "current_scheduled_depth", "current_outbox_depth")),
    "clock_coalesced_count",
    "clock_expired_count",
    "note_on_expired_count",
    "terminal_note_off_abandoned_count",
    "terminal_stop_abandoned_count",
    "current_outbox_depth",
    "max_outbox_depth",
    "max_send_attempts_per_public_pass",
    "outbox_capacity_failures",
    "delivery_capacity_safety_stops",
    "retry_window_safety_stops",
    "session_generation_advances",
    "explicit_clean_starts",
    "current_session_generation",
    *(f"session_ends_{reason}" for reason in _SESSION_END_REASONS),
)
INPUT_DIAGNOSTICS_COLUMNS = (
    INPUT_DIAGNOSTICS_PREFIX,
    "max_actual_encoder_sample_interval_us",
    "encoder_sample_intervals_above_1250_us",
)
RUNTIME_DIAGNOSTICS_COLUMNS = (
    RUNTIME_DIAGNOSTICS_PREFIX,
    "lv_timer_handler_count",
    "lv_timer_handler_inclusive_total_us",
    "lv_timer_handler_inclusive_max_us",
    "display_flush_count",
    "display_flush_inclusive_total_us",
    "display_flush_inclusive_max_us",
    "encoder_sample_window_max_interval_us",
    "encoder_sample_window_intervals_above_1250_us",
)

# Keep these names for callers that only know the original v2 protocol.
DIAGNOSTICS_PREFIX = V2_DIAGNOSTICS_PREFIX
DIAGNOSTICS_COLUMNS = V2_COLUMNS
DIAGNOSTICS_HEADER = ",".join(V2_COLUMNS)


def diagnostics_columns_for_row(row: str) -> tuple[int, tuple[str, ...]]:
    fields = row.split(",")
    schemas = ((2, V2_COLUMNS), (3, V3_COLUMNS))
    for version, columns in schemas:
        if fields[0] == columns[0] and len(fields) == len(columns):
            return version, columns
    raise RuntimeError(f"unrecognized diagnostics prefix or field count: {fields[0]!r}, {len(fields)}")


def diagnostics_header_for_row(row: str) -> str:
    _, columns = diagnostics_columns_for_row(row)
    return ",".join(columns)


def is_diagnostics_data_row(row: str) -> bool:
    try:
        parse_diagnostics_row(row)
    except RuntimeError:
        return False
    return True


def find_serial_port(explicit_port: str | None) -> str:
    if explicit_port:
        return explicit_port
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) != 1:
        raise RuntimeError(f"expected one /dev/cu.usbmodem* port, found {ports}")
    return ports[0]


def parse_diagnostics_row(row: str) -> tuple[int, dict[str, int]]:
    fields = row.split(",")
    version, columns = diagnostics_columns_for_row(row)
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError as error:
        raise RuntimeError("diagnostics row contains a non-integer value") from error
    return version, dict(zip(columns[1:], values, strict=True))


def input_diagnostics_header_for_row(row: str) -> str:
    input_diagnostics_columns_for_row(row)
    return ",".join(INPUT_DIAGNOSTICS_COLUMNS)


def input_diagnostics_columns_for_row(row: str) -> tuple[str, ...]:
    fields = row.split(",")
    if fields[0] != INPUT_DIAGNOSTICS_PREFIX or len(fields) != len(INPUT_DIAGNOSTICS_COLUMNS):
        raise RuntimeError(f"unrecognized input diagnostics prefix or field count: {fields[0]!r}, {len(fields)}")
    return INPUT_DIAGNOSTICS_COLUMNS


def parse_input_diagnostics_row(row: str) -> dict[str, int]:
    fields = row.split(",")
    columns = input_diagnostics_columns_for_row(row)
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError as error:
        raise RuntimeError("input diagnostics row contains a non-integer value") from error
    return dict(zip(columns[1:], values, strict=True))


def is_input_diagnostics_data_row(row: str) -> bool:
    try:
        parse_input_diagnostics_row(row)
    except RuntimeError:
        return False
    return True


def runtime_diagnostics_header_for_row(row: str) -> str:
    runtime_diagnostics_columns_for_row(row)
    return ",".join(RUNTIME_DIAGNOSTICS_COLUMNS)


def runtime_diagnostics_columns_for_row(row: str) -> tuple[str, ...]:
    fields = row.split(",")
    if fields[0] != RUNTIME_DIAGNOSTICS_PREFIX or len(fields) != len(RUNTIME_DIAGNOSTICS_COLUMNS):
        raise RuntimeError(
            f"unrecognized runtime diagnostics prefix or field count: {fields[0]!r}, {len(fields)}"
        )
    return RUNTIME_DIAGNOSTICS_COLUMNS


def parse_runtime_diagnostics_row(row: str) -> dict[str, int]:
    fields = row.split(",")
    columns = runtime_diagnostics_columns_for_row(row)
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError as error:
        raise RuntimeError("runtime diagnostics row contains a non-integer value") from error
    return dict(zip(columns[1:], values, strict=True))


def is_runtime_diagnostics_data_row(row: str) -> bool:
    try:
        parse_runtime_diagnostics_row(row)
    except RuntimeError:
        return False
    return True
