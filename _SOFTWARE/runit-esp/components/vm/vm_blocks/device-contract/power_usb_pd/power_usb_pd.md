# USB-PD sink

Asks a USB Power Delivery source for a voltage and current and reads its highest offer.

Works on a USB-PD sink (the AP33772S). The list of every offer is not a block: only the highest one is read.

## Settings

| Setting | Meaning |
|---|---|
| Device | The device this block works on. |
| Operation | What the block does with the device; it decides the inputs and outputs below. |
| When not enabled | An operation sent once when the enable turns off (none: the device is left as it is). Only a setting or an action can be sent. |

## Operations

### Request

Asks the source for a voltage and a current.

| Pin | Meaning |
|---|---|
| Voltage (input) | in volts. Unwired: the value set in the block's settings. |
| Current (input) | in mA. Unwired: the value set in the block's settings. |

### Read offer

Reads the highest-voltage offer of the source.

| Pin | Meaning |
|---|---|
| Voltage (output) | in volts. |
| Current (output) | in mA. |

## Behaviour

- A **setting** is sent on the first enabled pass and whenever one of its values changes; an **action** once on each rising edge of the enable; a **reading** on every enabled pass (gate it with Every to read less often) and written to the outputs.
- ENO is on while the device accepts the calls (a setting or a reading), or pulses on the pass an action was sent. A failed call is reported and ENO goes off.
- The device checks the values and refuses what it cannot do; nothing is clamped.
