# DRV8962 — dual full-bridge motor driver

Type `0x0049`. Contract: HBRIDGE (SYS_HBRIDGE.MD). The chip has no bus: every signal is a pin of another device. Two full bridges (motors, both directions) or four half bridges (solenoids). Two on the runIT board:

| ID | IN1-4 | EN1-4 | IPROPI1-4 | nSLEEP | nFAULT | VREF | PWM |
|---|---|---|---|---|---|---|---|
| 5 (#1) | ESP 21 / 47 / 48 / 45 | ESP 38 / 39 / 2 / 1 | ESP 7 / 6 / 5 / 4 (ADC) | TCA 9 | TCA 11 | DAC53202 OUT0 | 20 kHz (LEDC) |
| 6 (#2) | PCA CH 8..11 | PCA CH 12..15 | none (resistors only) | TCA 8 | TCA 10 | DAC53202 OUT1 | 1 kHz (the PCA frequency; at most 1526 Hz) |

## Create

`[05][id u16][00 00][49 00][drv8962_cfg_t]`: `{u8 topology, sys_io_pin_t pin[15], u32 pwm_freq_Hz, u32 ripropi_ohms[4], u32 current_limit_mA[4], u8 turn_off_at_ocp}` (1 + 45 + 4 + 16 + 16 + 1 = 83 B).

`pin[]` is indexed by `DRV8962_PIN_*`: IN1..4 = 0..3, EN1..4 = 4..7, IPROPI1..4 = 8..11, nSLEEP 12, nFAULT 13, VREF 14. **An unwired pin must be `SYS_IO_PIN_NONE`** (a zero pin is device 0 / pin 0, a real pin). Every wired pin's device must exist and can't be removed while the bridge does (`sys_device_use`; up to `CONFIG_SYS_DEVICE_MAX_DEPS` distinct devices).

| Field | Notes |
|---|---|
| `topology` | 0 two full bridges (channel 0 = half bridges 0+1, channel 1 = 2+3), 1 four half bridges; else `ERR_DEV_RANGE` |
| `pwm_freq_Hz` | 0 = 20 kHz; set on every IN pin, so it must suit the device behind it (a PCA9685 rejects above 1526 Hz: the create fails with `ERR_DEV_RANGE` from the PCA) |
| `ripropi_ohms[4]` | IPROPI resistor per half bridge; 0 = 3090 |
| `current_limit_mA[4]` | Per **channel**; 0 = no limit; at most 10000 (`ERR_DEV_RANGE`) |
| `turn_off_at_ocp` | 0 / 1: a channel over its limit is braked |

Steps: IN pins PWM at the frequency, duty 0 (`sys_io_prepare`: a device without SET_MODE such as the PCA9685 is fine); EN pins push-pull output, low, **locked**; nSLEEP output high, **locked**, a 2 ms wake wait; IPROPI pins ADC; VREF driven from the largest limit; every channel braked; nFAULT an input (pull-up where the device has one), subscribed inline to its `IO_EV_FALLING`, falling edge armed, **locked**. The IN, IPROPI and VREF pins are not locked (see Open). Uninstall coasts the channels, unlocks and releases what it made.

## Ops

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | Every channel braked |
| `0x0002` SUSPEND | – | – | Every channel coasted (EN low, IN duty 0), every one reached even if another fails |
| `0x0003` RESUME | – | – | Channels stay coasted: the next drive engages them |
| `0x0900..0x0903` HBRIDGE | SYS_HBRIDGE.MD | | SET_DRIVE: full bridge forward = IN1 100 %, IN2 = the PWM complement (slow decay); reverse the other way round. Half bridge: EN on, IN = the duty. GET_CURRENT: a read, allowed while suspended; the larger of a full bridge's two half bridges; 0 without IPROPI pins. A channel over its limit is latched, braked and published here too |
| `0x0300` ON_EVENT | `sys_event_t` | – | nFAULT fell (ignored while suspended) |
| `0xFF00` SET_CURRENT_LIMIT | `{u8 channel, u32 mA}` | – | 0..10000; VREF follows the largest limit |
| `0xFF01` CLEAR_FAULT | `{u8 channel}` | – | nSLEEP low 30 µs resets the chip's latches (**all** channels); only the channel's state is cleared, then it brakes. A 1 ms wake wait |
| `0xFF02` GET_FAULT | `{u8 channel}` | `{u8 fault, u8 reason}` | |

Channel ≥ the channel count → `ERR_DEV_BAD_PIN`.

**VREF**: ITRIP × AIPROPI = VREF / RIPROPI, so a channel's limit in mA maps to `limit × 212 µA/A × RIPROPI` mV (212 µA per A, datasheet); VREF takes the **largest** of the channels, each through its own resistor (legacy used the resistor of the channel index, which is wrong for a full bridge's second channel). No limit anywhere = 3300 mV (no regulation); clamped to 50..3300 mV.

## Faults

nFAULT only says "something is wrong", so the currents decide (a channel at its limit; else the one channel that was driving; else a chip-wide thermal or supply shutdown, every channel). Each faulted channel is latched, braked when `turn_off_at_ocp`, and published after the state is final (an inline listener may call back): `HBRIDGE_EV_OVERCURRENT` or `HBRIDGE_EV_THERMAL_OR_UVLO`, NOTIFY. The chip keeps the fault until CLEAR_FAULT.

## Open

- The IN pins are not locked: the PINS contract has no locked-duty op, and a locked pin refuses the IO writes this device makes. EN and nSLEEP are driven with `SET_LOCKED_LEVEL`.
- The driver does not look at its power mux (`POWER_SUPPLY`): it drives the bridge even if no VM source is on.
- No faulted-bridge test with a real overcurrent: the VM supply was never switched on.

## Changes from legacy

- HBRIDGE ops `0x09xx` with drive in PWM steps (-4095..4095, 12 bits; thousandths until 2026-10-12) (legacy: a contract struct with floats); the faults are events of the contract (NOTIFY).
- Integer current maths (212 µA/A) instead of floats; the limit and VREF values are range-checked instead of clamped, and VREF takes the largest limit with each channel's own resistor.
- EN and nSLEEP are locked (legacy had no locks); the nFAULT line is locked with its edge armed.
- GET_CURRENT no longer fails after publishing an overcurrent: the reading is returned, the event reports the fault.
- No driver mutex: the registry lock covers every op and the event handler.

## Tested

2026-10-11 on the runIT PCB (`scripts/board_tests.py`), VM supplies never switched on (no load): #2 on the PCA9685 and #1 on ESP pins create, lock the right pins and release them on REMOVE; a failed create (20 kHz on the PCA9685) leaves nothing registered or locked; nSLEEP high, VREF follows the limit (and the largest of several), SET_DRIVE / BRAKE / COAST / CLEAR_FAULT / SUSPEND / RESUME / RESET, ranges, bad channel, `ERR_DEV_IN_USE` on removing the DAC; #1 reads its IPROPI inputs. No real overcurrent or shutdown was provoked.
