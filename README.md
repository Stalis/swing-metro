# Swing Metro

Swing Metro is a hardware USB MIDI step sequencer for the Raspberry Pi Pico 2 W. A 4×4 button matrix, three encoders and an ST7735 display let you edit and play one 16-step program. The firmware uses C++17, PlatformIO, the Earle Philhower Arduino core, TinyUSB, Arduino_GFX, LVGL and LittleFS. It sends MIDI notes to a host; it does not produce USB audio.

## What works today

- One playback with 16 fixed steps. Each step can be enabled and has a note (36–127), velocity (1–127) and gate (1–100%). Notes currently use MIDI channel 1 (channel value 0 in code).
- Tempo 40–240 BPM and swing 50–90%; straight timing is swing 50. The Volume setting is stored and displayed but does not scale outgoing velocity.
- Explicit Off, Internal and External MIDI clock modes. Internal mode sends MIDI Clock; External mode follows incoming Clock and Start/Continue/Stop, handles clock loss and requires relock. It does not echo incoming Clock or fall back to internal timing.
- Save/Load/Reset, 16 user program slots and a separate current-program autosave slot. Saved program settings initialize the session when loaded while stopped. Storage uses versioned, CRC-checked A/B copies in LittleFS.
- A display with main and editor views, clock and storage dialogs, runtime diagnostics, native Unity tests and host-side Python tests.

Variable program length, repeat/legato step modes, parallel playback and songs are **planned**, not implemented. The R5 `ProgramBank` and `ProgramId` prepare their data boundaries but do not make them playable.

## Build and verify

Install PlatformIO CLI, `clang-format`, `clang-tidy`, Python 3.10+ and `make`. From the repository root:

| Command | Purpose |
| --- | --- |
| `make build` | Build the production `rpipico2` firmware. |
| `make test` | Run native Unity tests. |
| `make test-scripts` | Run host-side Python tests. |
| `make format-check` / `make format` | Check / apply the repository C/C++ format. |
| `make tidy` | Regenerate `compile_commands.json` and run `clang-tidy` with the ARM toolchain includes. |
| `make build-stage5-off` | Build with Stage 5 instrumentation disabled. |
| `make build-stage5-fault` | Build the deterministic fault-scenario variant. |
| `make verify` | Run formatting, tidy, both test suites and all three firmware builds in that order. |

The default environment is `rpipico2` (`rpipico2w` board, 1 MB LittleFS partition); `native` compiles selected hardware-independent sources. To upload the production firmware to a connected board, run `pio run -e rpipico2 -t upload` (add `--upload-port <port>` when needed). `pio device monitor -b 115200` opens the serial diagnostics. Hardware validation commands in the Makefile can flash the device and need connected MIDI/serial ports; see [diagnostics](docs/diagnostics.md) and [hardware](docs/hardware.md).

## Code map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp`, `src/application.{h,cpp}` | Arduino entry points and application composition/service order. |
| `src/engine/` | Session, playback, sequencer, scheduling, transport, clock, MIDI delivery and diagnostics. |
| `src/input/` | Input events, contexts, editor actions, dialogs and UI snapshot decoration. |
| `src/program/` | Program model, draft, RAM bank, codec, slot store and storage use cases. |
| `src/components/` | UI snapshot types and atomic cross-core `UiViewModel`. |
| `src/drivers/` | Pico alarm, USB MIDI, LittleFS, display and LVGL adapters. |
| `lib/` | Reusable button, encoder, input timing and context-input libraries. |
| `test/test_native/`, `scripts/` | Unity tests, host tests, diagnostics and hardware runs. |

`main.cpp` delegates `setup/loop` and `setup1/loop1` to one `Application`. Core 0 polls MIDI and physical inputs, services transport and delivery, applies edits and publishes a `UiSettings` snapshot. The current `Session` owns master tempo, clock mode, transport and one `Playback`; `Playback` owns the applied `Sequencer`. Input changes the editor or transport through application handlers. `StepPlanner` schedules note requests, `MidiDispatcher` handles due delivery to the USB MIDI sink, and `NoteLifecycle` tracks which launch the USB stack accepted. Core 1 owns LVGL and reads snapshots through `UiViewModel`; core 0 never calls LVGL. The view model uses atomic fields and a generation check so the UI does not consume a mixed snapshot.

Do not read or write LittleFS while transport is running: flash activity can break MIDI timing. Storage requests and autosave check the whole `Session` before access. USB-stack acceptance is not proof of host receipt or precise bus timing.

## Further reading and next work

- [Current architecture](docs/architecture/README.md), [hardware](docs/hardware.md), [diagnostics](docs/diagnostics.md) and [R6 result and device checklist](docs/refactoring/r6-final-result.md).
- Planned features: [program length](docs/features/program-length.md), [step modes](docs/features/step-modes.md), [parallel programs and MIDI](docs/features/multitrack-midi.md), [songs](docs/features/songs.md); [implementation order](docs/features/implementation-order.md).
- Historical design and validation: [refactoring plan](docs/plans/2026-09-27-gitnexus-plan-refactor-feature-foundation.md), [R4](docs/refactoring/r4-engine-result.md), [R5](docs/refactoring/r5-composition-result.md), [clock](docs/midi-clock/README.md) and [storage](docs/program-storage/README.md).

The recommended next sequence is program length, step modes, 16 simultaneous programs with MIDI routing, then songs. The user confirmed the post-refactor device check on 2026-10-03. The earlier R5 sink-only throughput probe is not a complete 16-program measurement.
