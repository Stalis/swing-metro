# R3 — input and application scenarios

Implemented on `codex/refactor-feature-foundation` after the user verified R2 on the device. R3 changes ownership of existing input and storage behavior; it does not add the planned step modes, songs, variable program length, or parallel channels.

## Result

| Package | Commit | Responsibility extracted |
| --- | --- | --- |
| R3.1 | `2ade70a` | `AppInputRouter` owns contexts, selected-step navigation, Shift restoration, stack capacity, and per-button gesture capture. |
| R3.2 | `5b1458f` | `StepEditor` applies note/velocity/gate edits and produces the editor snapshot; selection stays in the router. |
| R3.3 | `b30d1ba` | `MidiClockModal` owns open state, preview selection, clamping, and confirmation choice. The coordinator still applies the selected clock mode before closing the context. |
| R3.4 | `9b00d8d` | `ProgramStorageModal` owns action, slot, reset confirmation, result state, and creation of Save/Load/Reset commands. Existing storage enum values are unchanged. |
| R3.5 | `4b3ccfa` | `ProgramStorageRequest` queues one command and consumes it before calling `ProgramStorageController`. The controller remains the authority for the transport-running flash guard. |
| R3.6 | `fdae922` | `AppUiSnapshotBuilder` assembles the existing page, editor, MIDI clock, and storage fields from named inputs. |

`AppInputCoordinator` remains the public application facade and retains event ordering, modal exclusivity, transport actions, and the entry points used by `main.cpp`. It is 244 lines after R3, down from 441 before extraction. `main.cpp`, the `UiViewModel` mailbox, GUI code, program serialization, and the sequencer were not changed.

The storage sequence remains: confirmation enters `Busy` and queues a command; `loop()` publishes that snapshot; `processProgramStorage()` consumes and executes the command; the next snapshot reports `Success` or `Error`. Opening and closing the storage transport window still happen in `main.cpp`. A null storage controller still yields `NotMounted`, and `ProgramStorageController` still returns `TransportRunning` before flash access while playback is active.

## Verification

`make verify` passed on 2026-09-28: format check, clang-tidy, 351 native Unity tests, 45 host Python tests, and all three firmware builds. The native runner explicitly registers the new MIDI modal, storage modal, storage request, and snapshot-builder suites. The native tests cover step navigation and gesture capture, Shift restoration, MIDI Cancel/confirm and direct mode application, storage Cancel/Reset/Busy/result transitions, exactly-once requests, and rejection of storage operations during playback. PlatformIO used a temporary writable core directory linked to the installed packages and platforms because the default user cache was unavailable to the sandbox. Installed LVGL/platform headers emitted warnings, but the checks completed successfully.

| Firmware | RAM used | Flash used | R2 RAM / Flash |
| --- | ---: | ---: | ---: |
| Production | 169,976 B | 731,736 B | 169,964 B / 731,232 B |
| Instrumentation off | 169,684 B | 730,800 B | 169,672 B / 730,288 B |
| Fault scenarios | 173,124 B | 732,528 B | 173,112 B / 732,024 B |

GitNexus impact and change analysis were run before edits and each commit. The aggregate comparison with `ffcc5e1` reports CRITICAL risk because the coordinator sits on input, UI, and transport paths. The analyzer's process extraction was truncated at the final index (41 entry-point candidates dropped and six walks cut by budget), so absence from its flow list was not treated as proof of safety. Source review, native integration tests, and firmware builds supplied the regression evidence. The index was refreshed at each committed package; no incomplete index status was reported.

## Device check before R4

R3 firmware was built but not flashed. On the Pico 2 W, check a step's long press, selection of another step, and return to the main page; a held Shift across step navigation and a MIDI Clock dialog; MIDI Clock Cancel and each mode; storage Save/Load/Reset, both Cancel paths, Busy then Success/Error, and closing back to the previous context. Confirm that MIDI playback remains stable during input and that storage is rejected while transport runs. R4 can begin after this behavior is verified on the device.
