# Map

Maps a value from one range onto another: **X** from **In min**..**In max** gives **Y** from **Out min**..**Out max**, along a straight line or a curve. For example a joystick of 0..1023 onto a motor drive of -100..100 %, or a sensor's volts onto degrees.

With t = (X − In min) / (In max − In min), the place of X in its range (0 at In min, 1 at In max): Y = Out min + curve(t) × (Out max − Out min).

## Pins and settings

| | Meaning |
|---|---|
| X (input) | The value to map. |
| Y (output) | X mapped onto Out min..Out max. |
| In min, In max | The range of X. They may not be equal. |
| Out min, Out max | The range of Y: Y is Out min when X is In min, Out max when X is In max. |
| Curve | How Y follows X between the ends (below). |
| Stay in range | On (the default): X is held to In min..In max first, so Y never leaves Out min..Out max. Off: a linear map goes on past the ends; any other curve is always held. |
| ENO (Done) | On every pass Y was written. |

## Curves

| Curve | curve(t) | Use |
|---|---|---|
| Linear | t | a plain change of units |
| Logarithmic | log10(1 + 9t) | rises fast, then flattens: a volume knob, a sensor that saturates |
| Exponential | (10^t − 1) / 9 | starts slow, then rises fast: the way back from Logarithmic, a brightness that looks even to the eye |
| Square | t² | a gentle start with fine control at low values: a throttle |
| Square root | √t | a steep start: the way back from Square |
| S-curve | 3t² − 2t³ | eases in and out: a smooth start and stop of a movement |

Each pair is the other's way back: a value mapped with Logarithmic and then with Exponential (the ranges swapped) comes back the same; so do Square and Square root.

## Behaviour

- It computes on every pass it is enabled, so Y always follows X. Not enabled, Y keeps its last value and ENO is off.
- A range may run downwards: with Out min above Out max, Y falls as X rises (In min 0, In max 100, Out min 100, Out max 0 turns 30 into 70).
- The four ends are settings of the block, written into it with the program; they do not change while it runs.
