# Step modes — planned

`ProgramStep` currently has enabled, note, velocity and gate only. Add a mode and repeat count with a versioned persistence migration; old images become enabled/disabled one-shot steps with repeat count 1. A disabled step is a rest in every mode, not an implicit tie.

| Mode | Musical effect |
| --- | --- |
| One-shot | One Note On and its gate-controlled Note Off inside the step. “One-shot” describes one attack per visit, not a program that plays once. |
| Repeat | `N` equally spaced attacks within the actual step interval. `N=1` is exactly one-shot. Each attack uses the step's note and velocity. |
| Legato | Extends the previous accepted attack: suppress its planned Note Off, sends no new Note On and has no editable note of its own. |

For Repeat, propose `N=1..16` as a model range, subject to the [throughput gate](multitrack-midi.md). Count means total attacks, not additional attacks. Compute subdivisions from the step's swung start and following musical boundary, using tick plus phase, so rounding never accumulates. Gate is a percentage of each repeat subinterval; at gate 100, Off precedes the next On at the shared instant. Swing changes the enclosing interval, not the equal subdivision rule. External Clock still drives musical position; repeat events between incoming pulses use the transport's interpolated timing and must not be rounded to received F8 pulses.

For Legato, the planner must know the following step before the preceding gate deadline, including gate 1. A legato chain overrides that attack's ordinary gate deadline and produces one eventual Off. The chain can cross the program wrap; bound the scan to one program cycle so an all-legato program cannot loop forever. An initial legato after Start or after a rest is silent. Repeat→Legato extends only the final repeat attack. If an On was never accepted, the chain has no sounding note to prolong. A following attack closes the tie with Off before On, even when its pitch is unchanged; changing pitch is an attack, not a legato slide. Stop, restart, disconnect or invalidation terminate accepted notes according to the existing lifecycle and prevent a stale Off from affecting a new launch.

While a step is Legato, the GUI must not offer its pitch for editing; it may retain an inactive saved pitch for a later mode switch. Live mode edits take effect at a defined future boundary after reconciling queued events. A USB-accepted Off cannot be recalled and a new On must not be synthesized to imitate a tie. Persistence validation rejects invalid mode/repeat fields before application.

## Product decisions still required

- Maximum repeat count after full-system bandwidth testing, and whether overload rejects the edit or limits playback.
- Exact UI representation of inherited pitch, inactive gate and repeat controls, and whether old values are retained when switching modes.
- Whether legato may cross a song program change; the default proposal is to close the note there.
- Whether live articulation edits are offered immediately with boundary staging or restricted to Stop in the first release.
- How external receivers should handle overlap when future parallel routes share a channel and pitch; internal launch identity cannot force receiver voice identity.

## Acceptance scenarios

One-shot parity with R5; repeat 1/2/3/max with rounding, swing and gate 1/50/100; On–Legato–Legato–rest produces one On/Off; first-step/rest/all-legato silence; wrap tie; Repeat→Legato; same- and changed-pitch next attack; Stop/restart and delayed/rejected On; equal-time Off-before-On; external Clock jitter/loss/relock; GUI edit exclusion and old/new image migration.
