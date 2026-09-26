# 5.4 Load and fault scenarios

Status: software infrastructure complete; hardware matrix remains to be run on Pico 2 W.

`rpipico2-stage5-fault-scenarios` is a compile-time-only firmware environment. Its bounded,
allocation-free `FaultMidiMessageSink` decorates the real USB delivery sink; `rpipico2` contains
neither the injector nor the `FAULT` parser. The sink distinguishes MIDI message classes and only
faults Clock: accepted Start, Clock, and notes pass through the real USB sink.

Send `FAULT <scenario>` while stopped, wait for exactly one matching
`swing_metro_fault_v1,selected,<scenario>`, then send the ordinary `RUN` command. Selection resets
the sink state. Invalid, duplicate, out-of-order, or mismatched acknowledgements make the host
capture fail. Available scenarios are `baseline`, `retry_first_clock`, `sustained_backpressure`,
and `deterministic_disconnect`.

Normal `swing_metro_control_v1`, diagnostics v4, input v1, runtime v1, and timed report v1 are
unchanged. A requested fault capture instead writes `swing_metro_fault_run_report_v1` with the
separate `fault_injection` extension and `test_firmware_required: true`. `scenario.id` in supplied
metadata remains the independent study identifier and is not used as the injected scenario.

`pico_serial_run.py` and `pico_midi_run.py` accept optional `--fault-scenario`. Fault metadata
must contain a matching `fault_injection.scenario`; normal metadata must not contain that field.
The bounded matrix is run with `scripts/stage5_load_matrix.py`: one delegated fault firmware is
used for all six baseline cells at 40/120/240 BPM and swing 50/90,
plus one run for each non-baseline fault at 120/50. It preflights every metadata, output path, and
the `stage5-4-load-matrix-manifest.json` manifest; metadata must identify the fixed
`rpipico2-stage5-fault-scenarios` environment. The manifest records `upload_before_each_run: true`:
the script runs `pio run --environment rpipico2-stage5-fault-scenarios --target upload` before every
capture, then invokes the existing MIDI capture tool with argument lists only (no shell quoting).
Use `--pio /path/to/pio` only when `pio` is not on `PATH`. It never overwrites and fails immediately
on upload, capture, or predicate failure.

Baseline predicates require exactly one host Start and Stop, a nonzero host Clock capture equal to
both accepted device Clock and internal Clock attempts, no long/short/estimated-missing host Clock
intervals, and zero device failed publications and tick-queue overflows. Fault predicates are
only native-established counters: one Clock retry and recovery, one retry-window safety stop, or
one Clock disconnect. No new timing threshold is introduced.
Every matrix report must also have `device_local.fresh_boot_candidate: true` and
`comparison_eligibility: paired_capture_candidate`; boot-cumulative diagnostics from a reused boot
are rejected.

Build the dedicated firmware with `make build-stage5-fault`. Run the hardware matrix only after
flashing that environment; this step does not cover UI/encoder exercise, GPIO analysis, cable
reconnect, storage load, MPE, or optimization work.
