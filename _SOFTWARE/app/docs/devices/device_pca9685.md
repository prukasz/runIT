# PCA9685 PWM expander

Sixteen PWM outputs over I2C. On the runIT board one PCA9685 is installed at boot (device ID 3); add another one as a user device when it sits on an external board.

## Before you use it

- **One frequency for all 16 channels.** Setting the PWM frequency on one channel changes it for every channel of the chip. Servos want about 50 Hz, the H-bridge driven from channels 8–15 wants about 1 kHz: they can't run at their natural rates on the same chip.
- **Output enable is active-low.** With an OE pin configured, the outputs only drive while that pin is low. On the runIT board it is TCA6424A pin 0, owned by the device (other contracts can't read or change it).
- **No pin modes.** Every channel is an output: *Configure pin mode* is not a contract of this device. Use *Set output level* for on/off and *Set PWM duty* for anything in between.

## Install

- **I2C bus** 0 or 1, **address** 0x40–0x7F (set by the A0–A5 straps on the chip).
- **Output enable pin**: leave *Not connected* when OE is tied low on the board.

## Typical steps

1. *Set PWM frequency* on any channel (e.g. 50 Hz for servos).
2. *Set PWM duty* per channel.
3. Put both in an action to set a pose in one go.
