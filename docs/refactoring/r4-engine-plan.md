# R4 — engine decomposition

The user accepted the R3 firmware on the device on 2026-09-29. This plan implements the R4 stage of the [feature-foundation plan](../plans/2026-09-27-gitnexus-plan-refactor-feature-foundation.md) without changing musical behavior. OpenCode Plan examined the current engine and native build boundaries; OpenCode Build handles each implementation package. Codex checks the resulting source, runs verification, reviews GitNexus impact before symbol edits and change detection before commits.

## Invariants

- One playing program, 16 fixed steps, MIDI channel 0, the existing two-tick lookahead, swing and gate deadlines.
- One scheduled Note On/Off pair per enabled step; failed batch enqueue leaves the boundary and launch identity retryable.
- FIFO delivery, at most eight send attempts per pass, stable retry identity, and note-state changes only after sink acceptance.
- Existing Stop, session generation, invalidation, disconnect, clock coalescing and expiry behavior.
- No new LittleFS access during playback, no LVGL access from the main core, and no diagnostic wire-format changes.

## Packages

| Package | Boundary | Verification |
| --- | --- | --- |
| P1 — delivery contracts | Move sink and diagnostic types out of `transport_controller.h`; leave behavior untouched. | Full `make verify`, existing MIDI and diagnostics tests. |
| P2 — dispatcher | Move the unchanged `MidiDispatcher` class into its own header; keep `TransportController` API and initialization order. | Byte-for-byte class-body comparison, existing retry, Stop, clock and session tests, full `make verify`, unchanged firmware sizes. |
| P3 — note lifecycle | Give actual, projected and requested note launches a single state owner; retain `Sequencer` forwarding API while callers migrate. | Accepted vs queued/expired notes, stale Off, repeated Stop, disconnect/clean start, full `make verify`. |
| P4 — step planner | Move scheduling cursor and batch creation out of `Sequencer`; retain its compatibility API and single-program behavior. Add the new `.cpp` to native. | Tick 0, wrap, swing, gate 1/25/100, queue retry, overflow and baseline traces; full `make verify`. |
| P5 — integration | Compare musical event traces and delivery lifecycle across the new boundaries, document the result and hardware checklist. | Full `make verify`; device validation remains a separate observation. |

P1 precedes P2 so the dispatcher imports standalone delivery contracts. P3 precedes P4 to make note-state ownership explicit before separating schedule generation. No package introduces program length, Repeat, Legato, multiple tracks or MIDI fan-out; those remain feature work after R4.

`MidiDispatcher::servicePending` and `sendDue` are high-impact paths. Their bodies and ordering must be preserved during P2. An attempted simultaneous `.h/.cpp` split rewrote methods and was rejected before testing or commit. P2 therefore keeps the original method bodies in the new header, preserving timing and compiler inlining. A later source split is optional and needs separate timing validation. Short lifecycle helpers in the current class are not `constexpr`; extraction must not change their signatures or add `constexpr`. GitNexus process extraction is incomplete, so an absent process edge is never treated as proof that a path is unused.
