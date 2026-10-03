# R6 — final documentation and verification

R6 completed the documentation for `codex/refactor-feature-foundation` after R0–R5, without changing firmware behavior or implementing future musical features. Verification was run on 2026-10-03 against the code at `5272e52`; R6 commits change Markdown only.

## Delivered documents

- Root [README](../../README.md): current firmware, commands, architecture map, constraints and next work.
- [Current architecture](../architecture/README.md), with [the earlier target document](../architecture.md) explicitly marked historical.
- [Diagnostics](../diagnostics.md): corrected the current `Application` ownership after R5.
- Planned feature contracts: [program length](../features/program-length.md), [step modes](../features/step-modes.md), [parallel programs/MIDI](../features/multitrack-midi.md), [songs](../features/songs.md), and [implementation order](../features/implementation-order.md).

The specifications distinguish decisions already recorded in R5 from proposals needing product or load-test confirmation. In particular, R5 selected per-party retrigger for overlapping channel/pitch notes; it did not implement parallel playback.

## Automated verification

`make verify` passed in full: `format-check`, generated compilation database and `clang-tidy`, **366/366 native Unity tests**, **45/45 host Python tests**, then all three firmware environments. The first attempt reached PlatformIO's package lock in the sandbox and stopped after format checking; the successful full run used a writable temporary `PLATFORMIO_CORE_DIR` linked to the installed toolchains. No source changes or repeat full run were needed. Existing LVGL deprecation warnings are nonfatal.

| Firmware environment | R6 RAM | R6 Flash | Change from R5 |
| --- | ---: | ---: | --- |
| `rpipico2` | 171,292 / 524,288 B | 732,208 / 3,141,632 B | RAM 0; Flash −16 B |
| `rpipico2-stage5-instrumentation-off` | 170,996 / 524,288 B | 731,232 / 3,141,632 B | RAM 0; Flash −8 B |
| `rpipico2-stage5-fault-scenarios` | 174,436 / 524,288 B | 732,984 / 3,141,632 B | RAM 0; Flash −16 B |

R5's recorded values are in the [R5 result](r5-composition-result.md). The tiny Flash differences come from this rebuild; no firmware source changed in R6. The Markdown links in the R6 documents and the named current paths/commands were checked against the tree.

## GitNexus review

The index was current at the starting commit and refreshed with PDG after documentation commits. `detect_changes(scope=compare, base_ref=5272e52)` covered the R6 documentation diff: **10 files, 30 indexed sections, 0 affected processes, low risk**, with no `partial` or `truncated` flag. `detect_changes(scope=all)` was run before each commit.

The full-refactor comparison to `6317513ff61d2de3673c2dc3927b85b2987603b5` reports **119 files, 1,174 changed symbols, 134 affected processes, CRITICAL risk**. Its changed-symbol listing is **truncated** at 1,000, including when rerun with a higher CLI display limit, so this is not a clean exhaustive graph verdict. The index's process extraction also warns that 39 candidate entry points were omitted and six walks hit budget limits. An absent path in that comparison cannot be treated as unaffected. R0–R5 code regression evidence and the R6 full verification support the current behavior; the remaining graph coverage limit is explicit.

## Known limits and device check

- R5 firmware has not received a new manual musical-behavior check after the refactor. R5's USB sink spike measured local stack acceptance only, not the engine queue, normal GUI/Clock load or host receipt. The 16-program guarantee remains unmeasured.
- If user-slot Save succeeds but current autosave then fails, the persistent slot may contain new data while the RAM bank still has its older snapshot; the controller reports failure. A later bank refresh or transaction design is needed before songs or parallel playback rely on that edge case.
- Fixed 16-step one-shot programs, one playback and MIDI channel 1 remain the only implemented musical model. Song persistence, variable length, repeat/legato and multi-channel routing are specifications.

### Manual Pico 2 W checklist — pending, not claimed as performed

- [ ] Flash the R6 production build; confirm USB enumeration, boot, restored current program and start-up display.
- [ ] Run internal Clock through a full 16-step wrap at straight and swung settings; capture MIDI Clock and note timing.
- [ ] In External mode, test Start, Continue, Stop, loss at the timeout, relock and deliberate resume; confirm no silent internal fallback.
- [ ] Exercise gate 1, 25 and 100, same-pitch replacement and Off-before-On at shared boundaries, including the wrap.
- [ ] Stop mid-note, restart and check that no stale Off cuts off the new launch.
- [ ] Save/Load/Reset and autosave while stopped; try storage actions while running and confirm no LittleFS access.
- [ ] Check GUI and buttons/encoders while MIDI is active; confirm coherent snapshots and responsive controls.
- [ ] If using the fault build, test delayed/retried delivery, capacity stop, disconnect and explicit recovery; restore production afterward.

The branch is ready as a documented, automatically verified foundation for the recommended sequence: length → step modes → 16 parallel programs/MIDI routing → songs. Device acceptance and the full-system throughput gate remain separate work before making hardware timing or 16-program claims.
