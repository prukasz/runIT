# Read Voltage

Reads the voltage on an analog input (ADC) of an IO device, for example the ADS7128 or the ESP's own ADC pins, and writes it in **volts** to a float variable.

## Pins and settings

| | Meaning |
|---|---|
| Voltage (float, output) | Voltage on the pin, in volts. Required. |
| Device | The device that measures voltage. Only devices that can are listed. |
| Pin | The analog input to read. Pins the board reserves or another device uses are not offered. |
| ENO (Read) | True when the pin was read and the Voltage written this pass. |

## Behaviour

- Runs every pass while enabled and reads the pin each time. To sample at a fixed rate, connect a **Periodic** block's tick to *Run when*.
- Set the pin to the device's analog mode in the device's **default settings** (the ADS7128 starts all its pins as ADC).
- The value is what the device reports; scale it (a divider, a sensor curve) with an **Expression** block.
- A read the device refuses is an error of this block: ENO goes false and Voltage keeps its last value.
