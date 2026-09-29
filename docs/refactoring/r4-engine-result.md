# R4 — engine decomposition result

R4 was implemented on `codex/refactor-feature-foundation` after the user accepted the R3 firmware on the device. It changes ownership and source boundaries, not the musical feature set. The [R4 plan](r4-engine-plan.md) records the invariants and package sequence.

| Package | Commit | Result |
| --- | --- | --- |
| P1 | `b1f3546` | Standalone MIDI sink and transport diagnostic contracts. |
| P2 | `e4d43a5` | `MidiDispatcher` moved unchanged to its own header; `TransportController` retains its API and field initialization order. |
| P3 | `5cda6f1` | `NoteLifecycle` owns accepted, projected and requested launches, session generation, and clean/unknown remote state. `Sequencer` retains forwarding methods. |
| P4a | `9e39fc4` | Step definitions moved to `sequencer_step.h` without changing their layout or values. |
| P4b | `aa2e6b4` | `StepPlanner` owns the scheduling cursor and creates the existing Note On/Off batch. `Sequencer` still exposes `scheduleThrough(position, queue)`. |

`TransportController` is now 300 lines instead of 1052, with delivery types and dispatcher separated. `Sequencer` remains the compatibility facade for existing callers; its scheduling and note-state behavior live in independently testable components. The dispatcher remains header-only. Moving its method bodies to a `.cpp` was deliberately left out after an attempted rewrite failed the exact-body review; the accepted P2 copied its entire class body byte-for-byte and left firmware sizes unchanged.

## Regression evidence

Full `make verify` passed after P1, P2, P3, and P4b on 2026-09-29: format check, clang-tidy, 352 native Unity tests, 45 host Python tests, and production, instrumentation-off and fault-scenario firmware builds. P4a separately passed format check, 352 native tests and the production build. PlatformIO used a temporary writable core directory linked to installed packages because the default cache was unavailable to the sandbox.

The existing literal straight and swing reference traces in `test_sequencer.cpp` still pass across ticks 0–96. They check message kind, tick/phase, note, velocity, MIDI channel 0, launch identity and generation, including gate deadlines and the cycle wrap. The engine integration suite also passes for internal Start/Clock/note ordering, external Start/Continue/loss/relock, capacity and quota failure, retry identity, eight attempts per pass, pending Stop/Off ordering, same-pitch replacement, disconnect and explicit restart, and diagnostic result accounting. P3 added one direct test that a queued Note On does not become actual until accepted, and that an accepted note produces one stop-Off request. These are local test and build results, not hardware timing measurements.

| Firmware | R4 RAM / Flash | R3 RAM / Flash |
| --- | ---: | ---: |
| Production | 169,968 B / 731,880 B | 169,976 B / 731,736 B |
| Instrumentation off | 169,676 B / 730,944 B | 169,684 B / 730,800 B |
| Fault scenarios | 173,116 B / 732,672 B | 173,124 B / 732,528 B |

The final GitNexus comparison with `42edff5` reports CRITICAL impact on the engine and 179 affected process entries. `servicePending`, `sendDue`, the sequencer lifecycle methods, and scheduling were checked before edits; HIGH/CRITICAL risk was reported during the work. The process extractor is truncated (41 candidate entry points omitted and six walks cut by budget at the final index), and some C++ symbols resolve ambiguously, so absence from its flow list was never treated as proof of safety. Source review and the regression suite supplied the behavioral evidence.

## Device check before R5

R4 firmware was built but not flashed by the implementation agent. On the Pico 2 W, check an internal-clock run through the 16-step wrap at straight and swung settings, including gate 1/25/100 and Off-before-On at a shared boundary. Check Start, Stop and restart, external Start/Continue/Stop and clock loss/relock, and a same-pitch replacement after a delayed onset. Confirm that storage still refuses flash operations while transport runs and that the UI stays responsive. If a fault-scenario build is used, check retry and disconnect recovery against the previous R3 device behavior. Record any timing or MIDI trace capture separately; a successful build does not establish hardware timing parity. R5 begins after this device behavior is accepted.
