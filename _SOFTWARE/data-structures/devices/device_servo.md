# Servo — hobby servo on a PWM pin of another device

Type `0x0048`. No chip: it drives one PWM pin (a PCA9685 channel, an ESP GPIO) through the IO contract at 50 Hz. On the runIT board the servo headers are PCA9685 CH 0..7.

## Create

`[05][id u16][00 00][48 00][pwm_pin: u16 device, u8 pin][servo_type][byte_mode][invert][i16 angle_min][i16 angle_max][i16 default_pos][u16 min_us][u16 max_us][u16 deadband_us]` — `servo_cfg_t`.

| Field | Notes |
|---|---|
| `pwm_pin` | Must be a pin (`ERR_DEV_BAD_PIN` for `SYS_IO_PIN_NONE`); its device must exist and can't be removed while the servo does. Made a PWM pin (`sys_io_prepare`: a device without SET_MODE, such as the PCA9685, is fine), 50 Hz set |
| `servo_type` | 0 angle, 1 continuous (360) |
| `byte_mode`, `invert` | 0 / 1 |
| `angle_min_deg`, `angle_max_deg` | -360..360, min below max (angle servo) |
| `default_pos` | Degrees within the limits (angle) or percent -100..100 (continuous); where the servo goes at create and on HOME |
| `min_us`, `max_us` | The pulse limits: `min_us` < `max_us` ≤ 20000 |
| `deadband_us` | Continuous: the width around stop in which it does not move, below half the pulse range |

Any value out of range: `ERR_DEV_RANGE`. **The PCA9685 has one frequency for all 16 channels**: a servo on it sets 50 Hz for the others too (the DRV8962 #2 on CH 8..15 shares it). At create the servo moves to `default_pos`.

## Ops

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | Trim 0, no pulse (limp) |
| `0x0002` SUSPEND | – | – | No pulse; the last pulse is remembered |
| `0x0003` RESUME | – | – | The last pulse comes back (nothing if the servo was limp) |
| `0xFF00` HOME | – | – | To `default_pos` + trim |
| `0xFF01` SET_TRIM | `{i16 trim}` | – | -500..500: degrees (angle) or µs (continuous); applied by the next move. Not an output write: allowed while suspended |
| `0xFF02` SET | `{i16 value_x10}` | – | The position in tenths (905 = 90.5°, -250 = -25 %); in byte mode the byte × 10 (0..2550; 127 and 128 both stop a continuous servo). Out of range: `ERR_DEV_RANGE` |
| `0xFF03` GET_PULSE | – | `u16` µs | The pulse last sent, 0 = not driven. Allowed while suspended (0 then) |
| `0xFF04` RELEASE | – | – | No pulse: the servo goes limp until the next SET / HOME |

Angle: pulse = min_us + (angle − min) × (max_us − min_us) / (max − min), mirrored when `invert`, shifted by the trim. Continuous: stop is the middle of the pulse range (+ trim), the speed runs outwards from beyond the deadband.

## Open

- The PWM pin is **not locked**: the PINS contract has no locked-duty op (`SET_LOCKED_LEVEL` is a level), and a locked pin refuses the IO writes this device makes. So another command can write the servo's channel. Same for the DRV8962's IN pins.

## Changes from legacy

- Ops are own ops `0xFFxx` (legacy `0x80..0x84` behind the router); the pin is a `sys_io_pin_t`.
- The duty scale is 0..4095 (`IO_PWM_DUTY_MAX`), legacy 4096.
- SET while suspended is refused (legacy had no suspended state in the op path).

## Tested

2026-10-11 on the runIT PCB with the PCA9685 (`scripts/board_tests.py`): create errors, the pulse widths of an angle and a continuous servo at 0° / 45° / 90° / 180° and ±50 / ±100 %, trim, HOME, suspend / resume, release, ranges, remove. No servo was connected: the pulse is the value the device reports, not measured.
