# Set pin level

Drives a digital output pin on one of the board's IO devices (the ESP's own GPIO, an expander, and so on).

## Pins and settings

| | Meaning |
|---|---|
| Level (pin, optional) | The level to drive while the block is active. Overrides the **Level** setting. |
| Pin (pin, optional) | Selects the pin at run time; it must be one of the allowed pins. Overrides the **Pin** setting. |
| Device, Pin, Allowed pins | Which device and pin to drive, and the set of pins the selector may pick (it protects other pins). |
| When not active | What the pin does while the block is disabled: *low*, *high*, or *hold* (leave it as it is). |
| Level | The level driven while active when no pin is wired: *high* or *low*. |
| ENO | The write succeeded this pass. |

## Behaviour

- The pin is only written when the level (or pin) **changes**, so slow buses are not flooded with the same value.
- A pin that the board has locked for another purpose cannot be driven; the error says which pin.
