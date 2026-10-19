# Regulator

Switches an adjustable regulator on or off and sets its output voltage and current limit.

Works on a regulator with one output (the TPS55289 rails). The regulator keeps its voltage and limit while the output is off; the power manager checks the budget and may refuse.

## Settings

| Setting | Meaning |
|---|---|
| Device | The device this block works on. |
| Operation | What the block does with the device; it decides the inputs and outputs below. |
| When not enabled | An operation sent once when the enable turns off (none: the device is left as it is). Only a setting or an action can be sent. |

## Operations

### Output

Switches the regulator output on or off.

| Pin | Meaning |
|---|---|
| Output (input) | Off, On. Unwired: the value set in the block's settings. |

### Voltage

Sets the wanted output voltage.

| Pin | Meaning |
|---|---|
| Voltage (input) | in mV. Unwired: the value set in the block's settings. |

### Current limit

Sets the wanted output current limit.

| Pin | Meaning |
|---|---|
| Current limit (input) | in mA. Unwired: the value set in the block's settings. |

## Behaviour

- A **setting** is sent on the first enabled pass and whenever one of its values changes; an **action** once on each rising edge of the enable; a **reading** on every enabled pass (gate it with Every to read less often) and written to the outputs.
- ENO is on while the device accepts the calls (a setting or a reading), or pulses on the pass an action was sent. A failed call is reported and ENO goes off.
- The device checks the values and refuses what it cannot do; nothing is clamped.
