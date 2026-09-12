# Swing Metro

PlatformIO/Arduino firmware for the Raspberry Pi Pico 2 W (`rpipico2`, Earle Philhower core). The default environment builds firmware; `native` runs Unity tests against only `src/engine/sequencer.cpp`, `src/input/`, and `src/program/`.

## Commands

- `make test` — run the native Unity suite. Add each native test entry point to `test/test_native/main.cpp`; the runner does not discover test files automatically.
- `make format-check` — check C/C++ formatting; `make format` changes files.
- `make tidy` — regenerates `compile_commands.json` before running `clang-tidy` with the PlatformIO ARM standard-library paths.
- `make build` — build `rpipico2` firmware.
- `make verify` — runs `format-check`, `tidy`, native tests, then the firmware build in that order.

## Boundaries

- `src/main.cpp` owns Arduino setup/loop and wires hardware adapters to the application.
- Keep hardware-independent logic in `src/engine/`, `src/input/`, or `src/program/` so it remains covered by native tests. Put reusable hardware drivers in `lib/`; board- and application-specific adapters belong in `src/drivers/`.
- `UiViewModel` transfers snapshots between cores: the producer publishes and the UI reads. Do not access LVGL from the main core.
- Do not read or write LittleFS while transport is running; flash operations can break MIDI timing.

## C++ style

- Use `_paramName` for private fields, `CAPS_CASE` for constants and `constexpr`, `PascalCase` for types, and `camelCase` for all other identifiers. This is based on the code through `0457cecb3070251dc585b7c9e52938bceb94ff4b`.
- Format with the repository `.clang-format` (LLVM-derived: four spaces, attached braces, 100 columns).
