# Power monitor

Reads the voltage, current or power of a measured channel and arms its current alerts.

Works on one measured channel of a monitor (the INA3221). A reversed shunt reads a negative current.

## Settings

| Setting | Meaning |
|---|---|
| Device | The device this block works on. |
| Operation | What the block does with the device; it decides the inputs and outputs below. |
| When not enabled | An operation sent once when the enable turns off (none: the device is left as it is). Only a setting or an action can be sent. |

## Operations

### Read voltage

Reads the voltage of a channel.

| Pin | Meaning |
|---|---|
| Channel (input) | a number. Unwired: the value set in the block's settings. |
| Voltage (output) | in mV. |

### Read current

Reads the current of a channel.

| Pin | Meaning |
|---|---|
| Channel (input) | a number. Unwired: the value set in the block's settings. |
| Current (output) | in mA. |

### Read power

Reads the power of a channel.

| Pin | Meaning |
|---|---|
| Channel (input) | a number. Unwired: the value set in the block's settings. |
| Power (output) | in mW. |

### Set current alert

Arms a hardware alert that publishes an event when the current crosses a threshold.

| Pin | Meaning |
|---|---|
| Channel (input) | a number. Unwired: the value set in the block's settings. |
| Alert (input) | Warning, Critical. Unwired: the value set in the block's settings. |
| Threshold (input) | in mA. Unwired: the value set in the block's settings. |

## Behaviour

- A **setting** is sent on the first enabled pass and whenever one of its values changes; an **action** once on each rising edge of the enable; a **reading** on every enabled pass (gate it with Every to read less often) and written to the outputs.
- ENO is on while the device accepts the calls (a setting or a reading), or pulses on the pass an action was sent. A failed call is reported and ENO goes off.
- The device checks the values and refuses what it cannot do; nothing is clamped.
