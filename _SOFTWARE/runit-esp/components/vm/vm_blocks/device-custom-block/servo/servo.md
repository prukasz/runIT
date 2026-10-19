# Servo

Moves a servo to a position, sends it home or lets it go limp.

Works on a servo device. The position is in the servo's unit: degrees for an angle servo, percent for a continuous one, 0..255 in byte mode; a value outside its range is an error.

## Settings

| Setting | Meaning |
|---|---|
| Device | The device this block works on. |
| Operation | What the block does with the device; it decides the inputs and outputs below. |
| When not enabled | An operation sent once when the enable turns off (none: the device is left as it is). Only a setting or an action can be sent. |

## Operations

### Position

Moves the servo to a position.

| Pin | Meaning |
|---|---|
| Position (input) | in deg or %. Unwired: the value set in the block's settings. |

### Home

Moves the servo to its default position (plus the trim) once on each rising enable.

### Release

Stops the pulse so the servo goes limp, once on each rising enable.

### Trim

Sets a correction added to every move.

| Pin | Meaning |
|---|---|
| Trim (input) | in deg or us. Unwired: the value set in the block's settings. |

## Behaviour

- A **setting** is sent on the first enabled pass and whenever one of its values changes; an **action** once on each rising edge of the enable; a **reading** on every enabled pass (gate it with Every to read less often) and written to the outputs.
- ENO is on while the device accepts the calls (a setting or a reading), or pulses on the pass an action was sent. A failed call is reported and ENO goes off.
- The device checks the values and refuses what it cannot do; nothing is clamped.
