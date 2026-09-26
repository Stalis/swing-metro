# 5.4 Load and fault scenarios

Status: complete. The software infrastructure and the nine-cell Pico 2 W hardware matrix passed.

## Hardware result

The clean matrix was captured without operator interaction from commit
`3b6b20f99791cba0d69af41ac5d62ab61cb12d0b`, using the
`rpipico2-stage5-fault-scenarios` binary with SHA-256
`b730f246b6a058c67cbe8a840bb0f1243d7f63fd665711ffcee40c149500c61d`. Every run used a fresh
upload, a three-second endpoint-settle delay, and a 244-second requested duration. The ignored raw
artifacts and reports are in `data/stage5-4-results-r05/`; earlier `r01`-`r04` directories are
diagnostic attempts, not acceptance evidence.

| Scenario | BPM / swing | Device attempts / accepted | Host Clock | Result |
| --- | --- | ---: | ---: | --- |
| baseline | 40 / 50 | 3,904 / 3,904 | 3,904 | pass |
| baseline | 40 / 90 | 3,904 / 3,904 | 3,904 | pass |
| baseline | 120 / 50 | 11,712 / 11,712 | 11,712 | pass |
| baseline | 120 / 90 | 11,712 / 11,712 | 11,712 | pass |
| baseline | 240 / 50 | 23,424 / 23,424 | 23,424 | pass |
| baseline | 240 / 90 | 23,424 / 23,424 | 23,424 | pass |
| retry first Clock | 120 / 50 | 11,713 / 11,712 | 11,712 | one retry, one recovery |
| sustained backpressure | 120 / 50 | 1,128 / 0 | 0 | exactly one safety stop |
| deterministic disconnect | 120 / 50 | 1 / 0 | 0 | exactly one disconnect |

All reports are `paired_capture_candidate`. The baseline captures have exactly one Start and Stop,
zero failed diagnostics publications, zero tick-queue overflows, and zero delivery errors. Absolute
Clock drift over each baseline run was 31-121 us. CoreMIDI reported no long or short interval in
the final matrix. The retry scenario retained every accepted Clock at the host and showed exactly
one `RetryLater` followed by one recovery.

The sustained-backpressure hardware run exposed a lifecycle defect: after the terminal safety stop,
the completed retry window remained armed and the diagnostic counter could increase again on later
loop passes. `stopLaunch()` now clears that state. A native regression continues processing after
the stop and requires the counter to remain exactly one. `make verify` passes with 334 native and
29 Python tests.

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
capture, waits three seconds for the replacement CoreMIDI and CDC endpoints to stabilize, then
invokes the existing MIDI capture tool with argument lists only (no shell quoting).
Use `--pio /path/to/pio` only when `pio` is not on `PATH`. It never overwrites and fails immediately
on upload, capture, or predicate failure. `--resume` validates the exact existing manifest and every
complete report, rejects partial captures, and continues from the first missing cell.

Baseline predicates require exactly one host Start and Stop, a nonzero host Clock capture equal to
both accepted device Clock and internal Clock attempts, and zero device failed publications and
tick-queue overflows. Host long/short interval and estimated-missing heuristics remain recorded
observations rather than loss predicates: CoreMIDI can report a delayed/early batching pair while
the exact host/device counts and total drift remain intact. Fault predicates are only
native-established counters: one Clock retry and recovery, one retry-window safety stop, or one
Clock disconnect. No new timing threshold is introduced.
Every matrix report must also have `device_local.fresh_boot_candidate: true` and
`comparison_eligibility: paired_capture_candidate`; boot-cumulative diagnostics from a reused boot
are rejected.

Build the dedicated firmware with `make build-stage5-fault`. This step does not cover UI/encoder
exercise, GPIO analysis, physical cable reconnect, storage load, MPE, or optimization work; those
remain outside the acceptance claim above.

For a complete unattended capture, connect the Pico and run:

```sh
make stage5-load-matrix
```

The target builds the fault firmware, records the current revision, dirty-worktree state, exact ELF
SHA-256, resolved build flags, toolchain, dependencies, host, and hardware metadata, creates a
timestamped directory below `data/stage5-4-runs/`, runs all nine cells, and restores the production
firmware even when the matrix fails or is interrupted. Serial and MIDI endpoints are auto-detected
when unique; otherwise select them explicitly:

```sh
make stage5-load-matrix \
    STAGE5_SERIAL_PORT=/dev/cu.usbmodem101 \
    STAGE5_MIDI_PORT="Pico 2W"
```

Resume a stopped run with its printed result directory:

```sh
make stage5-load-matrix \
    STAGE5_OUTPUT_DIR=data/stage5-4-runs/20260926-120000 \
    STAGE5_RESUME=1
```

`STAGE5_DURATION_SECONDS` overrides the default 244 seconds per cell. A shortened run is useful for
checking the harness, but is not equivalent to the acceptance matrix.
