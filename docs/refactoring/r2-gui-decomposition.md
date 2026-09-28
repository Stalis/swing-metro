# R2 — GUI decomposition

Implemented on `codex/refactor-feature-foundation` after R1. The UI's appearance and input behavior are intended to remain unchanged. All LVGL work remains on core 1; `UiViewModel` still transfers a single generation-checked snapshot between cores.

## Result

| Package | Commit | Change |
| --- | --- | --- |
| R2.1 | `3d60460` | Moved `UiSettings` into `ui_snapshot.h` and grouped its 23 existing fields into main, editor, MIDI clock, storage, and page. Retained the packed atomics, `seq_cst`, deduplication, defaults, and retry protocol. Native tests check every field separately and compare complete concurrent snapshots. |
| R2.2 | `fb1fbfb` | Extracted `PicoDisplay`, `UiTheme`, and `StepGrid`. The grid retains its LVGL subject observer; the display retains the actual 6,400-color partial buffer, pins, rotation, 500 ms splash, and flush timing sequence. |
| R2.3 | `b1e1975` | Extracted `MainScreen` and `StepSettingsScreen`, including their widgets, subjects, caches, and grid ownership. `LvglUi` reads one snapshot, applies the page groups, and loads a screen only when the page changes. |
| R2.4 | `5288d16` | Extracted `MidiClockDialog` and `ProgramStorageDialog` with their top-layer widgets, visibility rules, cached values, and existing state rendering. `LvglUi` now assembles and routes the components. |

OpenCode Plan prepared local package plans; OpenCode Build authored the implementation. Codex reviewed each diff, ran independent checks, completed GitNexus `detect_changes` for staged and all changes, and made the commits. The schema-4 index was refreshed after each package. The final pre-commit graph checks were complete (neither partial nor truncated): R2.1 medium risk, three setup/loop flows; R2.2 low risk, no flows; R2.3 medium risk, one setup/style flow; R2.4 medium risk, one UI/style flow. LVGL callbacks were confirmed in source where graph impact returned `UNKNOWN`.

## Software verification

Final `make verify` passed on 2026-09-28: formatting, clang-tidy, 338 native Unity tests, 45 host Python tests, and production, instrumentation-off, and fault-scenario firmware builds. PlatformIO was run with a temporary writable core directory linked to the already installed read-only packages and platforms because the default user cache was unavailable to the sandbox. The temporary directory is outside the repository. Clang-tidy emitted warnings from installed platform/library headers but completed successfully.

| Firmware | RAM used | Flash used | R1 RAM / Flash |
| --- | ---: | ---: | ---: |
| Production | 169,964 B | 731,232 B | 169,956 B / 731,472 B |
| Instrumentation off | 169,672 B | 730,288 B | 169,664 B / 730,520 B |
| Fault scenarios | 173,112 B | 732,024 B | 173,104 B / 732,272 B |

The native test count fell from 345 to 338 because the eleven old mailbox tests were consolidated into four broader tests. The replacement tests cover initial read behavior, all 23 fields one at a time, full round-trip, and coherence under 20,000 concurrent writes/reads. The other native suites remain in the runner.

## Physical-device check still required

No board was flashed or exercised during R2. Before treating visual equivalence or timing as hardware-validated, check on the Pico 2 W:

- Boot splash duration, orientation, main screen labels, and both page transitions.
- Enabled and disabled step squares, active-step border, and redraw after returning to the main screen.
- Tempo, swing, volume, note, velocity, gate, and external clock status updates during playback.
- MIDI Clock dialog open, mode/selection changes, Cancel, close, and reopen.
- Program Storage action, slot and Cancel, Reset confirmation No/Yes, Busy, Success, and Error states; modal stacking over both pages.
- Live UI updates while transport runs and Stage 5 display-flush timing captures.

R3 input redesign and the future step modes, program length, parallel channels, and songs remain outside this GUI package.
