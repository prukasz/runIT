# ESP32 native GPIO

The ESP32-S3's own pins: digital in and out, interrupts, ADC voltage reads and PWM. The board installs it at boot as device ID 0; there is nothing to configure.

## Before you use it

- **Configure a pin first.** A pin has to get a mode (*Configure pin mode*) before it can be driven or read.
- **Pins owned by other devices are locked.** GPIO 42, for example, is the ADS7128 alert input: every contract on it is refused, even a read.
- **A pin in use keeps its mode.** Changing the mode of a pin another device or contract already configured is refused (`ERR_IO_PIN_ALREADY_IN_USE`); reset the pin first.
- **No DAC here.** For an analog output voltage use the DAC53202.
- **Open-drain with pull-up** is listed but the adapter refuses it; use open-drain with an external resistor.

## Typical steps

1. *Configure pin mode*: Output Push-Pull.
2. *Set output level*: on.
3. Record both as an action to switch the pin from a VM ACTION block or on an error.
