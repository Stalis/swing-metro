# R5 — composition and playback result

R5 code and verification were completed on `codex/refactor-feature-foundation` from `d71b0cc`. The user revised the USB-stack acceptance target from 1 ms to 5 ms after the initial device probe; 1 ms remains a future optimization. The measured sink path meets the revised target in one run, but the complete 16-party path, host receipt, and R5 firmware on the device remain unmeasured. The [local R5 plan](r5-composition-plan.md) records the contracts and this distinction.

## Device probe

The connected Pico 2 W was flashed with a temporary firmware built against the installed Earle Philhower/Adafruit TinyUSB core. The source in `scripts/r5_throughput_probe/` calls the production `UsbMidiMessageSink::send()` and therefore uses `Adafruit_USBD_MIDI::writePacket()`. A USB CDC `g` starts 257 repeat boundaries at 3,906.25 µs spacing. The first boundary has 256 Note On messages; the next 255 each have 256 Note Off followed by 256 Note On; the final boundary sends 256 Note Off to release every test note. Sixteen distinct notes are sent across all 16 channels. This is 131,072 attempted note packets over about one second. No current engine queue, UI work, or MIDI Clock was included, making this a lower-bound sink test rather than a full-system validation.

| Measure | Observed | Revised R5 gate |
| --- | ---: | ---: |
| Accepted by `writePacket()` | 131,072 / 131,072 | All |
| `RetryLater` responses | 12,402 | Diagnostic |
| Accepted more than 1,000 µs late | 89,844 | Diagnostic |
| Maximum first-attempt lateness | 4,769 µs | Diagnostic |
| Maximum acceptance lateness | 4,773 µs | ≤5,000 µs |
| Maximum measured probe pass | 1,117 µs | Diagnostic |
| Disconnects | 0 | 0 |

The exact serial report is `R5_SPIKE,accepted=131072,expected=131072,retry_later=12402,disconnected=0,late_over_1ms=89844,max_first_attempt_late_us=4769,max_accept_late_us=4773,max_pass_us=1117`. Probe ELF SHA-256: `e0813fed111ee86605af391a7e14e6298bc7c17bf96bde27acde5a24696d203a`.

To repeat from the repository root with a connected Pico, build and upload the probe with `pio run -d scripts/r5_throughput_probe -e rpipico2 -t upload --upload-port <CDC-port>`, then run `python scripts/r5_throughput_probe/capture.py <CDC-port>` (requires pyserial). Restore the normal firmware with `pio run -e rpipico2 -t upload --upload-port <CDC-port>`. The hardware run used a temporary writable PlatformIO core because the default cache was inaccessible to the sandbox.

This measures local USB-stack acceptance, not physical bus delivery or host receipt. The 4,773 µs maximum leaves only 227 µs against the revised 5 ms threshold in this one run. It does not establish a worst-case guarantee under Clock and UI work. The 1 ms target was missed; 89,844 packets were accepted later than 1 ms. No host capture, queue high-water, Clock lateness, or normal-UI run was performed. The production queue capacity of 16, per-tick quota of eight, and eight dispatch attempts per pass were not raised or validated for 16 concurrent programs. Future parallel playback needs a full-system measurement and likely capacity work before any latency claim.

The prior `rpipico2` firmware was restored with PlatformIO upload and verified by successful flash/read-back. The restored firmware reports 169,968 B RAM and 731,880 B flash, matching R4; its ELF SHA-256 is `140d029620f5c5b53393a27e904e4abafacfa460114e7ba02d25491f32ab956f`. The device again enumerated as Pico 2W. This is upload evidence, not a fresh manual musical-behavior check.

## Implemented packages

| Commit | Change |
| --- | --- |
| `f3c040d` | Real Pico 2 W USB-MIDI sink spike and its original 1 ms result. |
| `bbc1b78` | Strong `ProgramId` for slots 0–15, fixed-capacity revisioned RAM `ProgramBank`, and separate editable `ProgramDraft`. Also recorded the revised 5 ms feasibility gate. |
| `b63ed95` | `Session` owns master tempo, clock, transport, and one `Playback`; `Playback` owns the applied `Sequencer`, cursor and note lifecycle. The transport scheduling path now uses `Playback`; existing editor/test `Sequencer` APIs remain. Storage checks session activity. |
| `226b78f` | Save/Load/Reset and autosave use the draft and bank contracts. A successful user-slot Save updates the bank; Load does not silently replace a saved bank entry. Slot 16 remains current autosave only. |
| `5552259` | `Application` owns hardware and service wiring; `main.cpp` is a 15-line four-entry-point adapter. Core 0 service order and core 1 LVGL ownership stay intact. |
| `864a5f6` | Live draft snapshot reads current applied steps and swing without LittleFS or a duplicate Playback program cache. |

`Program` remains persisted pattern data and saved initial settings; it has no transport cursor or delivery state. The current edit is a copy with an optional source ID. A saved program's tempo/clock initializes the common session when loaded while stopped; another program's saved settings do not switch master timing mid-playback. Live step and swing edits keep their existing order. Session generation is exposed through the transport controller. `UiViewModel` still crosses cores only as a published snapshot. The v1 Program image and A/B storage format remain unchanged.

Future migration is specified, not implemented: v1 imports with `length=16`, `one-shot`, repeat count 1, and channel 0. A new versioned container would be written only by explicit Save with transport stopped. A future song stores `ProgramId` references to user slots; overwriting slot N changes what subsequent references resolve to. Slot 16 has no `ProgramId`. Required programs must enter RAM before transport starts. Same-channel/same-note collisions are future per-party retriggers, whose overlapping Note Off effect is determined by the MIDI receiver. Variable length, step modes, routing, parallel playback, and songs are outside R5.

## Verification and graph review

Targeted native and firmware builds passed after each code package. The final `make verify` passed after `864a5f6`: formatting and clang-tidy passed, all 366 native Unity tests passed, all 45 host script tests passed, and all three firmware variants built. No code changed after that run. New native tests cover ProgramId bounds, bank revisions, draft isolation, stopped storage access, active-playback edit snapshots without flash, Save/Load/Reset behavior, and Session versus direct-Sequencer traces at straight/swing 50/75 and gate 1/25/100. Existing suites continue covering Off-before-On and channel 0, wrap and enqueue retry, internal/external Clock, Start/Stop/Continue, loss/relock, delayed On, disconnect, and UI snapshot consistency.

| Firmware environment | RAM | Flash |
| --- | ---: | ---: |
| `rpipico2` | 171,292 B | 732,224 B |
| `rpipico2-stage5-instrumentation-off` | 170,996 B | 731,240 B |
| `rpipico2-stage5-fault-scenarios` | 174,436 B | 733,000 B |

GitNexus upstream impact was checked before edits, including CRITICAL risk for the transport schedule path; UNKNOWN results were checked against source calls. The final diff versus `d71b0cc` affected 28 files, 182 indexed symbols, and 191 processes and was rated CRITICAL, primarily because ownership moved into `Application`. Every package had `detect-changes --scope all` before its commit, then an index refresh. The process extractor itself reported truncated walks, so absent graph paths were not interpreted as proof of safety.

## Known limits and device checklist

If writing a user slot succeeds but writing the current autosave then fails, the persistent user slot may contain the new data while the RAM bank retains its previous snapshot. The controller reports failure; it does not claim a successful Save. No song or parallel-playback consumer reads the bank in R5. A later bank refresh or transactional multi-slot save is needed before such consumers rely on this edge case.

R5 firmware has not been flashed or manually checked on the Pico after the refactor. The spike device was restored to the earlier R4 firmware. Before claiming device acceptance, check:

- Straight and swing traces, gate 1/25/100, step wrap, channel 0, and Note Off before Note On.
- Internal and external Clock, Start/Stop/Continue, loss/relock, delayed On/retry, and disconnect recovery.
- Save/Load/Reset and autosave only when the whole Session is stopped; no LittleFS access during transport.
- UI responsiveness and coherent `UiViewModel` snapshots with LVGL on core 1.
- For future parallel playback, host receipt and a full-device load run with queue high-water, lost events, Clock lateness, and `loop()` duration; the sink-only spike does not cover these.
