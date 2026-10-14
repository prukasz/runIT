# IO pin

Drives or reads a pin of a device: a level, a PWM duty or frequency, a voltage.

Works on one pin of a device that has pins (the ESP's own, an expander, a PWM chip).

## Settings

| Setting | Meaning |
|---|---|
| Device | The device this block works on. |
| Operation | What the block does with the device; it decides the inputs and outputs below. |
| When not enabled | An operation sent once when the enable turns off (none: the device is left as it is). Only a setting or an action can be sent. |

## Operations

### Set level

Drives a pin high or low.

| Pin | Meaning |
|---|---|
| Pin (input) | a number. Unwired: the value set in the block's settings. |
| Level (input) | Low, High. Unwired: the value set in the block's settings. |

### Toggle

Flips the level of a pin once on each rising enable.

| Pin | Meaning |
|---|---|
| Pin (input) | a number. Unwired: the value set in the block's settings. |

### Read level

Reads the level of a pin.

| Pin | Meaning |
|---|---|
| Pin (input) | a number. Unwired: the value set in the block's settings. |
| Level (output) | Low, High. |

### Set PWM duty

Sets the PWM duty of a pin.

| Pin | Meaning |
|---|---|
| Pin (input) | a number. Unwired: the value set in the block's settings. |
| Duty (input) | in steps; 0 to 4095. Unwired: the value set in the block's settings. |

### Set PWM frequency

Sets the PWM frequency of a pin (a device with one frequency for all its pins ignores the pin).

| Pin | Meaning |
|---|---|
| Pin (input) | a number. Unwired: the value set in the block's settings. |
| Frequency (input) | in Hz. Unwired: the value set in the block's settings. |

### Set mode

Configures what a pin does.

| Pin | Meaning |
|---|---|
| Pin (input) | a number. Unwired: the value set in the block's settings. |
| Mode (input) | Input, Input pull-up, Input pull-down, Output, Open drain, Open drain pull-up, PWM, ADC, DAC. Unwired: the value set in the block's settings. |

### Reset pin

Puts a pin back in its power-on state once on each rising enable.

| Pin | Meaning |
|---|---|
| Pin (input) | a number. Unwired: the value set in the block's settings. |

### Read voltage

Reads the voltage of an analog pin.

| Pin | Meaning |
|---|---|
| Pin (input) | a number. Unwired: the value set in the block's settings. |
| Voltage (output) | in mV. |

### Set voltage

Drives an analog output pin to a voltage.

| Pin | Meaning |
|---|---|
| Pin (input) | a number. Unwired: the value set in the block's settings. |
| Voltage (input) | in mV. Unwired: the value set in the block's settings. |

## Behaviour

- A **setting** is sent on the first enabled pass and whenever one of its values changes; an **action** once on each rising edge of the enable; a **reading** on every enabled pass (gate it with Every to read less often) and written to the outputs.
- ENO is on while the device accepts the calls (a setting or a reading), or pulses on the pass an action was sent. A failed call is reported and ENO goes off.
- The device checks the values and refuses what it cannot do; nothing is clamped.
