# Device control

Resets, suspends or resumes a device, or every device at once.

Every device answers these. Choosing the device 0xFFFF (every device) suspends newest first and resumes oldest first.

## Settings

| Setting | Meaning |
|---|---|
| Device | The device this block works on. |
| Operation | What the block does with the device; it decides the inputs and outputs below. |
| When not enabled | An operation sent once when the enable turns off (none: the device is left as it is). Only a setting or an action can be sent. |

## Operations

### Reset

Puts the device's outputs off and its settings back to defaults.

### Suspend

Makes the device's outputs safe; it refuses writes until it is resumed.

### Resume

Lets a suspended device work again.

## Behaviour

- A **setting** is sent on the first enabled pass and whenever one of its values changes; an **action** once on each rising edge of the enable; a **reading** on every enabled pass (gate it with Every to read less often) and written to the outputs.
- ENO is on while the device accepts the calls (a setting or a reading), or pulses on the pass an action was sent. A failed call is reported and ENO goes off.
- The device checks the values and refuses what it cannot do; nothing is clamped.
