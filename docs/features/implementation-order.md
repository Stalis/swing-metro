# Implementation order after R6

All four features below are specifications, not current firmware behavior. The base is the R5 single-playback architecture; [R6](../refactoring/r6-final-result.md) records its verification and remaining device check.

| Option | Order | Advantage | Cost and risk |
| --- | --- | --- | --- |
| **A — recommended** | **Program length → step modes → 16 parallel programs/MIDI routing → songs** | Tests cycle and cursor rules before articulation, then note identity on one playback before fan-out. Songs can depend on the settled playback and cycle contracts. | Song editing arrives last; full-system throughput and collision policy still need early decisions. |
| B — composition first | Program length → step modes → songs on one playback → 16 parallel programs/MIDI routing | A complete pattern-to-song workflow arrives earlier and exercises `ProgramId` storage sooner. | Later multi-playback work must adapt a song cursor initially built around one playback; risk of coupling a song to global transport. |
| C — throughput first | 16 parallel programs/MIDI routing on fixed one-shot patterns → program length → step modes → songs | Hardware capacity and shared-note semantics are tested early. | Largest risky change comes first; repeat and variable-length integration still require renewed load and lifecycle checks. |

Option A changes one principal model contract at a time: length defines cycle completion, step modes define within-step note lifecycle, multi-playback defines track and route identity, and songs then consume stable cycle and ID contracts. It also allows each new contract to be tested against the existing one-playback trace before increasing concurrency. The R5 sink spike reduces uncertainty but does not replace the full-system gate for parallel playback.

Before starting any option, settle swing parity for odd-length cycles, the exact repeat maximum, receiver behavior under the R5 per-party retrigger policy, song transition/Continue behavior and versioned persistence migration. Preserve v1 decode compatibility and the Stop-only flash rule through every stage.
