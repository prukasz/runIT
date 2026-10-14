# Every (periodic tick)

Pulses ENO for one pass every **period** while it is enabled. Put its ENO on the enable of another block to make that block run every period.

## Pins and settings

| | Meaning |
|---|---|
| Period (pin, optional) | Overrides the period constant. A period of 0 from the pin pauses the ticking. |
| Period, Time base | The constant used while the pin is unwired: a number (more than 0) and a unit (ms, s, min, h). |
| ENO (Tick) | On for exactly one pass per period. |

## Behaviour

- The first tick comes on the pass the block becomes enabled, then every period. Disabling stops it; enabling again restarts the phase.
- The deadline moves by exactly one period each time, so the rate does not drift.
- There is never more than one tick per pass. If a slow pass missed ticks, they are dropped, not replayed in a burst; this is reported once per slow episode.
- The pass (about 10 ms) is the resolution: a period shorter than a pass drops a tick on every pass.
- It counts **program time**: time spent stopped, paused or stepping is not counted, and under the slow-motion setting the period stretches with the program.
