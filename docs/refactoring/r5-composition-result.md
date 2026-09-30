# R5 — throughput gate result; implementation stopped

The mandatory R5 throughput gate **failed on 2026-09-30**. No Program/Session/Playback refactor was started, and R5 is not complete. This is the stop condition in the [local R5 plan](r5-composition-plan.md). The repository remained at `d71b0cc` on `codex/refactor-feature-foundation` during the probe.

## Device probe

The connected Pico 2 W was flashed with a temporary firmware built against the installed Earle Philhower/Adafruit TinyUSB core. The source in `scripts/r5_throughput_probe/` calls the production `UsbMidiMessageSink::send()` and therefore uses `Adafruit_USBD_MIDI::writePacket()`. A USB CDC `g` starts 257 repeat boundaries at 3,906.25 µs spacing. The first boundary has 256 Note On messages; the next 255 each have 256 Note Off followed by 256 Note On; the final boundary sends 256 Note Off to release every test note. Sixteen distinct notes are sent across all 16 channels. This is 131,072 attempted note packets over about one second. No current engine queue, UI work, or MIDI Clock was included, making this a lower-bound sink test rather than a full-system validation.

| Measure | Observed | Gate |
| --- | ---: | ---: |
| Accepted by `writePacket()` | 131,072 / 131,072 | All |
| `RetryLater` responses | 12,402 | Diagnostic |
| Accepted more than 1,000 µs late | 89,844 | 0 |
| Maximum first-attempt lateness | 4,769 µs | ≤1,000 µs |
| Maximum acceptance lateness | 4,773 µs | ≤1,000 µs |
| Maximum measured probe pass | 1,117 µs | Diagnostic |
| Disconnects | 0 | 0 |

The exact serial report is `R5_SPIKE,accepted=131072,expected=131072,retry_later=12402,disconnected=0,late_over_1ms=89844,max_first_attempt_late_us=4769,max_accept_late_us=4773,max_pass_us=1117`. Probe ELF SHA-256: `e0813fed111ee86605af391a7e14e6298bc7c17bf96bde27acde5a24696d203a`.

To repeat from the repository root with a connected Pico, build and upload the probe with `pio run -d scripts/r5_throughput_probe -e rpipico2 -t upload --upload-port <CDC-port>`, then run `python scripts/r5_throughput_probe/capture.py <CDC-port>` (requires pyserial). Restore the normal firmware with `pio run -e rpipico2 -t upload --upload-port <CDC-port>`. The hardware run used a temporary writable PlatformIO core because the default cache was inaccessible to the sandbox.

This measures local USB-stack acceptance, not physical bus delivery or host receipt. Since the lower-bound test misses the requested 1 ms deadline, adding Clock and GUI load cannot make this observation a passing full-system result. The result does not prove that every possible future scheduler or USB buffering design is infeasible; it proves the current sink/core path does not meet the mandatory gate at this load. No host capture, queue high-water, Clock lateness, or normal-UI run was performed after this failing first gate.

The prior `rpipico2` firmware was restored with PlatformIO upload and verified by successful flash/read-back. The restored firmware reports 169,968 B RAM and 731,880 B flash, matching R4; its ELF SHA-256 is `140d029620f5c5b53393a27e904e4abafacfa460114e7ba02d25491f32ab956f`. The device again enumerated as Pico 2W. This is upload evidence, not a fresh manual musical-behavior check.

## Remaining work

No R5 production code, native tests, migration, or composition changes were made. Consequently `make verify`, R5 firmware-size comparison, and final regression graph analysis are not claimed. Before resuming, either demonstrate a changed USB path that meets the same 1 ms criterion on the device or explicitly revise that requirement. The pending device checklist remains: straight/swing and gate traces, Off-before-On, internal/external Clock and Start/Stop/Continue, loss/relock, delayed-onset retry, disconnect recovery, stopped-only LittleFS, and responsive UI.
