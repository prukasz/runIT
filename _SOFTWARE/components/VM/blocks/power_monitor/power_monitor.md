# Power Monitor

Reads the **voltage** and the **current** of one channel of a power monitor device (for example the INA3221 on the board) and writes them to two variables.

## Pins and settings

| | Meaning |
|---|---|
| Voltage (float, output) | Bus voltage of the channel, in **volts**. Optional: leave it unconnected and the voltage is not read. |
| Current (float, output) | Current of the channel, in **amperes**; negative when it flows the other way round (the device's *reversed shunt* setting flips the sign). Optional. |
| Device | The power monitor to read. Only devices that can measure voltage and current are listed. |
| Channel | The channel of that device (an INA3221 has three). |
| ENO (Read) | True when every connected output was read and written this pass. |

## Behaviour

- Runs every pass while enabled and reads the device each time. To read at a fixed rate, connect a **Periodic** block's tick to *Run when*.
- A read the device refuses (no such channel, bus error) is an error of this block, ENO goes false and the outputs keep their last values.
- The device itself (I2C address, shunt direction, alert pins) is set up on the Board view.
