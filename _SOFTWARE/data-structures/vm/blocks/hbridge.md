# H-bridge

Drives a motor channel of a motor driver, brakes or coasts it, and reads its current.

Works on one channel of a motor driver. A full bridge drives a motor both ways, a half bridge one way.

## Settings

| Setting | Meaning |
|---|---|
| Device | The device this block works on. |
| Operation | What the block does with the device; it decides the inputs and outputs below. |
| When not enabled | An operation sent once when the enable turns off (none: the device is left as it is). Only a setting or an action can be sent. |

## Operations

### Drive

Drives a channel: positive forward, negative reverse (a half bridge only forward).

| Pin | Meaning |
|---|---|
| Channel (input) | a number. Unwired: the value set in the block's settings. |
| Drive (input) | in %; -100 to 100. Unwired: the value set in the block's settings. |

### Brake

Shorts the motor terminals once on each rising enable.

| Pin | Meaning |
|---|---|
| Channel (input) | a number. Unwired: the value set in the block's settings. |

### Coast

Releases the motor (outputs off) once on each rising enable.

| Pin | Meaning |
|---|---|
| Channel (input) | a number. Unwired: the value set in the block's settings. |

### Read current

Reads the current of a channel.

| Pin | Meaning |
|---|---|
| Channel (input) | a number. Unwired: the value set in the block's settings. |
| Current (output) | in mA. |

## Behaviour

- A **setting** is sent on the first enabled pass and whenever one of its values changes; an **action** once on each rising edge of the enable; a **reading** on every enabled pass (gate it with Every to read less often) and written to the outputs.
- ENO is on while the device accepts the calls (a setting or a reading), or pulses on the pass an action was sent. A failed call is reported and ENO goes off.
- The device checks the values and refuses what it cannot do; nothing is clamped.
