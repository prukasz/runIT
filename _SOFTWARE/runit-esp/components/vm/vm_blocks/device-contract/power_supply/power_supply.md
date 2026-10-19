# Power mux

Selects which of two sources feeds the load and reads which one is on.

Works on a power mux: two switched sources into one load. At most one is on; a change switches the old one off first.

## Settings

| Setting | Meaning |
|---|---|
| Device | The device this block works on. |
| Operation | What the block does with the device; it decides the inputs and outputs below. |
| When not enabled | An operation sent once when the enable turns off (none: the device is left as it is). Only a setting or an action can be sent. |

## Operations

### Select source

Feeds the load from nothing, source A or source B.

| Pin | Meaning |
|---|---|
| Source (input) | Off, A, B. Unwired: the value set in the block's settings. |

### Read source

Reads which source is on.

| Pin | Meaning |
|---|---|
| Source (output) | Off, A, B. |

## Behaviour

- A **setting** is sent on the first enabled pass and whenever one of its values changes; an **action** once on each rising edge of the enable; a **reading** on every enabled pass (gate it with Every to read less often) and written to the outputs.
- ENO is on while the device accepts the calls (a setting or a reading), or pulses on the pass an action was sent. A failed call is reported and ENO goes off.
- The device checks the values and refuses what it cannot do; nothing is clamped.
