# Servo

Moves a servo device to a position. The servo itself (pin, type, angle range, pulse limits, trim, default position) is set up in the device; the block only steers it.

## Pins and settings

| | Meaning |
|---|---|
| Position (float, optional) | The position, in the unit of the servo: degrees (angle servo), -100..100 (continuous servo) or 0..255 (a servo steered by byte). Overrides the **Position** setting. |
| Device | The servo to steer. Only servo devices are listed. |
| Position | The position used while the Position input is unwired. |
| When not active | What the servo does while the block is not enabled: *hold* (stay where it is), *default* (go to the servo's default position) or *release* (stop the pulse, the servo goes limp). |
| ENO | The servo follows the value this pass. |

## Behaviour

- The servo is only commanded when the value (or the action) **changes**.
- A value outside the servo's range is an error and the servo does not move; it is never clamped.
- The trim lives in the servo device and is applied to every move.
