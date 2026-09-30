# R5 — composition and playback models

R5 follows the approved [feature-foundation plan](../plans/2026-09-27-gitnexus-plan-refactor-feature-foundation.md) on `codex/refactor-feature-foundation`. Its purpose is to give Program, Session, and Playback separate owners while preserving the single-program musical trace. OpenCode Plan inspected the current source; Codex owns implementation and verification.

## Throughput feasibility gate

Before designing parallel playback, measure the real `UsbMidiMessageSink::send()` path on Pico 2 W. The future maximum is 16 programs × 16 MIDI channels × 16 attacks per sixteenth. At 240 BPM, a sixteenth lasts 62,500 µs and the attack spacing is 3,906.25 µs. With gate 100, each repeat boundary can require 256 Note Off followed by 256 Note On: 512 USB-MIDI packets at one position. The sustained load is 131,072 packets/s or 524,288 payload bytes/s before USB overhead. The current scheduled queue holds 16 events, permits eight packets per tick, and the dispatcher attempts eight sends per pass.

The user revised the R5 feasibility limit on 2026-09-30 to **zero lost Note On/Off and USB-stack acceptance within 5,000 µs** of each scheduled position at the stated maximum. The earlier 1,000 µs target is deferred optimization, not an R5 gate. Record first-attempt and acceptance lateness, `RetryLater`, queue high-water, processing duration, Clock lateness, and host receipt separately. The sink-only probe establishes feasibility of the USB path and permits the structural R5 packages; it is not a full-pipeline guarantee. A later full-pipeline run with Clock and normal UI load is needed before claiming that future parallel playback meets the 5,000 µs bound. Do not substitute host timestamps for device timestamps or silently coalesce/restrict the load. A sink-only failure of the revised gate blocks R5; otherwise preserve the unmeasured integration risk explicitly.

## Contracts for packages after a passing gate

1. `ProgramId` is a strong user-slot identity `0..15`; slot 16 remains the current autosave snapshot and cannot be referenced by a song. A fixed RAM `ProgramBank` stores optional program values and revisions. The editable current draft is a separate copy with optional source ID; edits do not change a bank entry until Save.
2. `Program` contains persisted pattern data and saved initial settings, never cursor, delivery state, or UI pointers. `Session` owns one master tempo/clock, transport lifecycle, generation, and one `Playback` in R5. `Playback` owns the selected program identity, applied data, local cursor, `Sequencer`, `StepPlanner`, and `NoteLifecycle`. Stored tempo/clock initialize a stopped session on load. Live edits to the active program must keep their present scheduling behavior.
3. Storage checks session-wide transport activity before every LittleFS operation. Preserve current A/B images and wire version in R5. A future version imports v1 as length 16, one-shot, repeat 1, channel 0, and writes the new format only on an explicit stopped Save. Rewriting an existing user slot updates references to that `ProgramId`.
4. `main.cpp` becomes board composition and four Arduino entry points. Core 0 keeps the existing serial, external MIDI, transport, input, diagnostics/alarm, UI snapshot, and stopped-storage order. Core 1 alone calls LVGL. `UiViewModel` retains its atomic snapshot protocol.

The future same-channel/same-note policy is retrigger per party. Independent playback positions and note identities remain necessary; MIDI receivers determine how overlapping Note Off messages sound. R5 does not implement variable length, step modes, routing, parallel playback, or songs.

## Verification and commits

For every package, run GitNexus upstream impact before editing each existing symbol; report HIGH/CRITICAL and confirm `UNKNOWN` by source search. Use OpenCode Build for large packages, then independently review the diff and run targeted native and firmware checks. Before each commit, run `detect-changes --scope all` and reject partial/truncated output. Commit packages separately and refresh the index. Add pure `.cpp` files to the native filter and register test entry points explicitly. After all R5 code, run one full `make verify`, repeat it only if code changes, then document firmware sizes, final graph diff, test results, hardware checklist, and a clean tree in `r5-composition-result.md`.

Regression tests must preserve R0 straight/swing traces and channel 0 Off-before-On ordering; gate 1/25/100, wrap, failed enqueue retry, internal/external Clock, Start/Stop/Continue, loss/relock, delayed On, disconnect recovery, LittleFS idle, and coherent cross-core UI snapshots remain acceptance criteria.
