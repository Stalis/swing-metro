# GitNexus Engineering Plan

> Task: Add reproducible Stage 5.5.2 external MIDI Clock loss/relock hardware validation.
> Evidence verified at commit 642c063e3babfd21a648c43523e115139d3fc2f7; GitNexus index refreshed this session to that commit.
> Evidence provenance schema 2; global dirty digest 0a9c85780067d9afcd0764f307b60891e3cee927ee11eaeb5ec7826d10fd82cd; cited-path manifest 11 sorted entries; exact generated plan path excluded.

## Objective (§1)

Provide one production-firmware command that uploads the firmware, drives a deterministic external 120 BPM sequence through USB MIDI, captures device MIDI plus Serial diagnostics, validates loss/relock semantics, and writes reproducible CSV/JSON evidence.

## Current Behaviour (§2–3)

- [verified] `RUN` is unsuitable: `pollSerialRunCommand` forces `MidiClockMode::Internal`, sets tempo/swing, and toggles transport (`src/main.cpp:504-562`).
- [verified] Incoming realtime packets are polled before `TransportController::process`; External mode consumes Start/Continue/Stop/Clock without echoing local Clock (`src/main.cpp:709-724`, `src/engine/transport_controller.h:818-889`).
- [verified] Clock loss is 250 ms; loss stops playback and a later Clock relocks before Continue resumes it (`src/engine/external_midi_clock.h:24-105`).
- [verified] Existing runtime diagnostics support asynchronous snapshot-and-reset and are printed only after transport stops (`src/engine/runtime_timing_diagnostics.h:18-85`, `src/main.cpp:374-490`).
- [verified] Existing Python capture/report helpers already provide MIDI port selection, callback capture, strict Serial rows, metadata, CSV output, and report generation.

## Findings (§4–5)

- [graph] `query(external MIDI clock ...)` located `handleExternal`, `ExternalMidiClock::update`, existing native loss/relock tests, and the Python capture stack.
- [graph] `impact(SerialRunCommandParser, upstream, maxDepth=2)`: LOW, exactly two direct consumers: `src/main.cpp` and `test_serial_run_command.cpp`.
- [graph] `impact(pollSerialRunCommand, upstream, maxDepth=2)` and `impact(exportInternalTimingDiagnostics, ...)`: LOW; direct caller is only `loop`.
- [verified] No firmware MIDI-path changes are required; the missing seam is a capture command that observes External mode without selecting Internal mode.
- PDG was intentionally skipped for this test-infrastructure task; control ordering is source-verified and will be regression-tested.

## Proposed Changes (§6)

1. Extend `SerialRunCommandParser` with an explicit internal/external command kind while preserving `RUN <ms> <bpm> <swing>`; add `EXTERNAL_RUN <ms> <bpm> <swing>`.
2. Add an External capture lifecycle in `main.cpp`: reject unless mode is External and transport stopped; reset runtime/input windows before acknowledgement; never toggle transport or change program settings; request/export the final snapshot only after duration and final Stop.
3. Add `scripts/stage5_external_clock_validation.py`: production upload, input/output endpoint selection, deterministic phase scheduler, sent/received event logs, metadata/report/summary, strict acceptance, safe final Stop.
4. Add Python tests for sequence construction and acceptance predicates, a Make target, and document the exact operator gate and reproduction command.

## Implementation Sequence (§7)

1. Firmware protocol: parser model/tests, then External capture state in `main.cpp`; keep existing Internal RUN behavior byte-compatible.
2. Host harness: pure sequence/analysis functions first, mocked unit tests second, hardware orchestration last.
3. Workflow/docs: Make target, operator confirmation gate, Stage 5.5.2 command and evidence contract.

## Test Strategy (§8)

- Native: both command kinds, CRLF/bounds/overflow recovery, malformed External command, preserved Internal fields.
- Python: exact 120 BPM/24 PPQN phases; bounded deterministic jitter; Stop→Continue; >250 ms loss; Clock→Continue relock; final Stop; deadline monotonicity.
- Acceptance negatives: any echoed F8/FA/FB/FC, missing loss counter, notes absent before/after relock, unbalanced lifecycle, queue/delivery failures, incomplete Serial rows, metadata mismatch, overwrite attempt.
- Verification: `make test`, `make test-scripts`, `make verify`; hardware acceptance later through `make stage5-external-clock STAGE5_PATTERN_CONFIRMED=1`.

## Implementation Context (§11)

```yaml
implementation_context:
  task_summary: "Implement automated Stage 5.5.2 external MIDI Clock loss/relock capture and validation."
  evidence_provenance:
    schema_version: 2
    head_commit: "642c063e3babfd21a648c43523e115139d3fc2f7"
    generated_plan_path: "docs/plans/2026-09-27-gitnexus-plan-external-clock-validation.md"
    global_dirty_digest:
      algorithm: "sha256"
      canonicalization: "gitnexus-evidence-provenance-v2 NUL-framed UTF-8 records"
      value: "0a9c85780067d9afcd0764f307b60891e3cee927ee11eaeb5ec7826d10fd82cd"
    cited_path_manifest:
      - {path: "Makefile", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:e5665db8c85e47f3d13796ae2f713353735b36fd1582c19b2d2a7e103c7dc3ce", index_digest: "sha256:e5665db8c85e47f3d13796ae2f713353735b36fd1582c19b2d2a7e103c7dc3ce", worktree_digest: "sha256:e5665db8c85e47f3d13796ae2f713353735b36fd1582c19b2d2a7e103c7dc3ce", untracked_digest: "absent"}
      - {path: "docs/midi-timing-and-ump/05-diagnostics-and-validation/05-hardware-validation.md", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:3362656bb58f3b20a68f09636cb3a3dd13287f83f18b61d7dcb3973f883530d6", index_digest: "sha256:3362656bb58f3b20a68f09636cb3a3dd13287f83f18b61d7dcb3973f883530d6", worktree_digest: "sha256:3362656bb58f3b20a68f09636cb3a3dd13287f83f18b61d7dcb3973f883530d6", untracked_digest: "absent"}
      - {path: "scripts/pico_midi_run.py", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:b3a5e1d8b8559eda880f9f99e7ca3dec590158712ee1e766eeb881ca53216503", index_digest: "sha256:b3a5e1d8b8559eda880f9f99e7ca3dec590158712ee1e766eeb881ca53216503", worktree_digest: "sha256:b3a5e1d8b8559eda880f9f99e7ca3dec590158712ee1e766eeb881ca53216503", untracked_digest: "absent"}
      - {path: "scripts/pico_run_report.py", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:8dd980bbd782a6c0bb26073ed57f8459220dcabd683df4bd604a8b3c6edc17c8", index_digest: "sha256:8dd980bbd782a6c0bb26073ed57f8459220dcabd683df4bd604a8b3c6edc17c8", worktree_digest: "sha256:8dd980bbd782a6c0bb26073ed57f8459220dcabd683df4bd604a8b3c6edc17c8", untracked_digest: "absent"}
      - {path: "scripts/stage5_production_internal_matrix.py", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:255532e956f1afcd71dfdf7ac32cca5a473724c67e40c00ec7104acc99d47e56", index_digest: "sha256:255532e956f1afcd71dfdf7ac32cca5a473724c67e40c00ec7104acc99d47e56", worktree_digest: "sha256:255532e956f1afcd71dfdf7ac32cca5a473724c67e40c00ec7104acc99d47e56", untracked_digest: "absent"}
      - {path: "src/engine/external_midi_clock.h", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:3154fd6b13fb358f3ef0fd9e32f1d6c08c2457cc058e4db4ceea125d549dfea0", index_digest: "sha256:3154fd6b13fb358f3ef0fd9e32f1d6c08c2457cc058e4db4ceea125d549dfea0", worktree_digest: "sha256:3154fd6b13fb358f3ef0fd9e32f1d6c08c2457cc058e4db4ceea125d549dfea0", untracked_digest: "absent"}
      - {path: "src/engine/runtime_timing_diagnostics.h", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:bedb48cf5a0416164b8deaa7dbf45c299cee9ef02f75e5c6a9c5082d98e147b7", index_digest: "sha256:bedb48cf5a0416164b8deaa7dbf45c299cee9ef02f75e5c6a9c5082d98e147b7", worktree_digest: "sha256:bedb48cf5a0416164b8deaa7dbf45c299cee9ef02f75e5c6a9c5082d98e147b7", untracked_digest: "absent"}
      - {path: "src/engine/transport_controller.h", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:7c3e79d81fbd9897ead3eebc3cc8eccba565d05a809850e22e6c509601086517", index_digest: "sha256:7c3e79d81fbd9897ead3eebc3cc8eccba565d05a809850e22e6c509601086517", worktree_digest: "sha256:7c3e79d81fbd9897ead3eebc3cc8eccba565d05a809850e22e6c509601086517", untracked_digest: "absent"}
      - {path: "src/input/serial_run_command.h", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:17080aa18d27906bb124e4b36a56248f29a36dbdceac8ee8751f3900194c4548", index_digest: "sha256:17080aa18d27906bb124e4b36a56248f29a36dbdceac8ee8751f3900194c4548", worktree_digest: "sha256:17080aa18d27906bb124e4b36a56248f29a36dbdceac8ee8751f3900194c4548", untracked_digest: "absent"}
      - {path: "src/main.cpp", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:026987d880019c0fc23ccfdb14eea5692aefb23f52f533330c54e8c679490275", index_digest: "sha256:026987d880019c0fc23ccfdb14eea5692aefb23f52f533330c54e8c679490275", worktree_digest: "sha256:026987d880019c0fc23ccfdb14eea5692aefb23f52f533330c54e8c679490275", untracked_digest: "absent"}
      - {path: "test/test_native/input/test_serial_run_command.cpp", object_kind: {head: "regular", index: "regular", worktree: "regular", untracked: "absent"}, state: "clean", rename_from: null, rename_to: null, head_digest: "sha256:557eeaa860bf9aece00335981f2f4c8731b19090a9e0aa0afcb7ed0f3f63b739", index_digest: "sha256:557eeaa860bf9aece00335981f2f4c8731b19090a9e0aa0afcb7ed0f3f63b739", worktree_digest: "sha256:557eeaa860bf9aece00335981f2f4c8731b19090a9e0aa0afcb7ed0f3f63b739", untracked_digest: "absent"}
  files_to_modify:
    - {file: "src/input/serial_run_command.h", symbols: ["SerialRunCommand", "SerialRunCommandParser"], intended_change: "Represent and parse Internal versus External capture commands."}
    - {file: "test/test_native/input/test_serial_run_command.cpp", symbols: ["serial command tests"], intended_change: "Cover External grammar and Internal compatibility."}
    - {file: "src/main.cpp", symbols: ["pollSerialRunCommand", "updateSerialRun", "loop capture lifecycle"], intended_change: "Observe External mode without switching mode or transport; bracket diagnostics snapshots."}
    - {file: "scripts/stage5_external_clock_validation.py", symbols: ["new harness"], intended_change: "Send/log deterministic phases, capture output/Serial, validate and report."}
    - {file: "scripts/tests/test_stage5_external_clock_validation.py", symbols: ["new tests"], intended_change: "Test schedule and acceptance without hardware."}
    - {file: "Makefile", symbols: ["stage5-external-clock"], intended_change: "Expose one reproducible command with operator gate and selectors."}
    - {file: "docs/midi-timing-and-ump/05-diagnostics-and-validation/05-hardware-validation.md", symbols: [], intended_change: "Document implemented protocol, command, artifacts and acceptance."}
  tests:
    - {file: "test/test_native/input/test_serial_run_command.cpp", scenarios: ["RUN remains byte-compatible", "EXTERNAL_RUN parses valid bounds", "malformed/out-of-range External input is rejected", "overflow recovery works for both kinds"]}
    - {file: "scripts/tests/test_stage5_external_clock_validation.py", scenarios: ["deterministic phase order and deadlines", "jitter remains bounded and zero-sum", "accept one loss and successful relock", "reject realtime echo, missing post-relock notes, bad lifecycle/counters", "refuse overwrite and metadata mismatch"]}
  verification_commands:
    - "make test"
    - "make test-scripts"
    - "make verify"
  assumptions:
    - "Check through mocked and hardware port enumeration that Pico exposes one selectable MIDI input and one selectable MIDI output endpoint."
    - "Check before hardware execution that External mode and all 16 Gate-100 steps persist across production upload; firmware command must reject any non-External mode."
  open_questions:
    - "Hardware evidence remains pending until the user prepares External mode/all-steps Gate 100 and runs the new Make target."
  avoid:
    - "Do not change ExternalMidiClock or TransportController musical semantics."
    - "Do not print Serial data from the realtime MIDI handling path."
    - "Do not compare host send timestamps with device-local timestamps as one clock domain."
    - "Do not overwrite existing evidence or claim GPIO/interrupt latency."
```

## Assumptions and Open Questions (§12)

- [assumed] CoreMIDI exposes Pico input/output endpoints under a unique matching name; the harness must support explicit selectors.
- [assumed] The saved current program can be prepared as External mode with all 16 steps enabled and Gate 100 before upload; the firmware command independently rejects wrong mode.
- The MCP impact payload retained a one-commit-behind banner after refresh, while the typed context resource confirms the index at HEAD; all named direct dependents were source-verified.
- Deferred: UI visual-state automation, GPIO analyzer measurements, and input shift-register/PIO work.

## Definition of Done (§13)

- Existing Internal RUN remains compatible and all native tests pass.
- External capture never selects Internal mode, changes tempo/swing, or emits Serial inside realtime handling.
- Host artifacts contain sent phase timestamps, received MIDI, strict diagnostics, metadata, report and summary without overwrite.
- Automated predicates prove no realtime echo, one loss, balanced notes before/after relock, no queue/delivery failures, and safe final Stop.
- `make verify` passes and the hardware command is documented but not falsely marked complete before its device run.
