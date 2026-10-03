# Variable program length — planned

## Contract

Add `length` in the inclusive range 1–16 to `Program`; retain a fixed array of 16 steps. Only the first `length` steps play. Shortening does not erase hidden step values; lengthening restores them. A cycle completes exactly after `length` step boundaries and yields one cycle-complete signal for future song repetition counting. `Playback` owns its position; the absolute master transport tick and MIDI Clock never reset at a program wrap.

While stopped, a valid edit applies immediately to the draft and applied playback. During playback, stage the new value and apply it at the next cycle boundary, after already scheduled events for the old cycle are reconciled. Show the pending value in the GUI. If that safe staging path is not implemented in the first increment, disable live length edits rather than changing modulo during the planner's lookahead. The GUI keeps 16 physical positions, dims the inactive tail and moves selection to a valid step when necessary.

Swing parity follows the absolute musical sixteenth count, including odd lengths such as 3; wrapping the program does not restart a swing pair. Internal and external Clock follow the same musical boundary rule. An external Start begins at step 0, Continue retains the current cursor, and relock does not create a second wrap or attack.

## Persistence and invariants

Introduce a new versioned program image. Decode existing v1 images as length 16; preserve their 16 step values. Validate length before applying a program. The first migration write happens only on explicit Save while stopped; never migrate by autosave or by writing flash during playback. A failed decode or invalid 0/17 must not partially replace the applied program. The current A/B revision and CRC behavior must remain intact.

- Applied length is always 1–16; cursor is always inside its active prefix.
- At most one cycle completion is emitted per wrap, even if scheduling retries after a full queue.
- Hidden steps create no MIDI requests, including requests in a lookahead window after a length change.
- Live edit has an explicit pending/applied distinction; no already accepted MIDI event is retroactively changed.

## Acceptance scenarios

Test lengths 1, 2, 3, 15 and 16; reject 0 and 17. Check wrap and one cycle signal, the restored hidden tail, length 3 with swing, internal and external Start/Continue/loss/relock, stopped and live edits at the final boundary, queue retry without duplicate attacks, GUI selection and dimming, v1 migration, new encode/decode/CRC and storage while stopped only. Keep length 16 event traces identical to R5.
