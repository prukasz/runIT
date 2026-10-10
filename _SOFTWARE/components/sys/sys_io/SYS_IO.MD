# sys_io — the IO contract

Not a module with state: a set of device ops (contract `SYS_DEVICE_CONTRACT_IO` = `0x01`, ops `0x01xx`) that mean the same on every device that has them (PWM expander, GPIO expander, the ESP's own pins). A device lists the IO ops it can do in its op table (SYS_DEVICE.MD); an op it doesn't have answers `ERR_DEV_UNKNOWN_OP`. The wrappers in `sys_io.c` only build the `sys_device_call`, so an internal call and a remote command run the same code.

| File | Contents |
|---|---|
| `sys_io.h` | The `SYS_IO_CONTRACT` table (op IDs, handler / entry macros), arg structs, `sys_io_mode_e`, `sys_io_intr_mode_e` (machine-read `//#ref-enum` annotations), `sys_io_pin_t`, wrappers |
| `sys_io.c` | The wrappers |
| `sys_pins.h` / `sys_pins.c` | The PINS contract (below): table, pin-state helper for devices, wrappers |

## Pins

`sys_io_pin_t {u16 device, u8 pin}` (3 bytes, also the field of a create struct). An unused pin is `SYS_IO_PIN_NONE` (device `0xFFFF`): a zero-filled field is device 0 / pin 0, a real pin. `sys_io_pin_valid(p)`.

## Ops

`[05][id u16][op u16][args]`; payloads are packed little-endian structs. The contract is the table `SYS_IO_CONTRACT(X)` (`X(NAME, number, args)`): `IO_OP_<NAME>`, `io_args_<NAME>_t`, `IO_HANDLER(NAME)` and `IO_ENTRY(NAME)` all come from it (SYS_DEVICE.MD, "Contract tables").

| Op | Wrapper | Args | Answer |
|---|---|---|---|
| `0x0100` SET_LEVEL | `sys_io_set_level(p, level)` | `{u8 pin, u8 level}` | – |
| `0x0101` TOGGLE | `sys_io_toggle(p)` | `{u8 pin}` | – |
| `0x0102` GET_LEVEL | `sys_io_get_level(p, &level)` | `{u8 pin}` | u8 |
| `0x0103` SET_PWM_DUTY | `sys_io_set_pwm_duty(p, duty)` | `{u8 pin, u32 duty}` | – |
| `0x0104` SET_PWM_FREQ | `sys_io_set_pwm_frequency(p, Hz)` | `{u8 pin, u32 frequency_Hz}` | – |
| `0x0105` SET_MODE | `sys_io_set_mode(p, mode)` | `{u8 pin, u8 mode}` (`sys_io_mode_e`) | – |
| `0x0106` RESET | `sys_io_reset(p)` | `{u8 pin}` | – (pin back to its power-on state) |
| `0x0107` CONFIGURE_INTR | `sys_io_configure_intr(p, &cfg)` | `{u8 pin, sys_io_intr_cfg_t}` | – |
| `0x0108` GET_VOLTAGE | `sys_io_get_voltage(p, &mV)` | `{u8 pin}` | i32 mV (signed: a negative reading is reported, not wrapped) |
| `0x0109` SET_VOLTAGE | `sys_io_set_voltage(p, mV)` | `{u8 pin, i32 voltage_mV}` | – |

- **Duty** is `0..IO_PWM_DUTY_MAX` (4095, 12-bit); a device with another resolution scales or rejects with `ERR_DEV_RANGE`.
- **Frequency:** a device with one frequency for all pins (PCA9685) ignores `pin`.
- **Modes** (`sys_io_mode_e`): input (plain / pull-up / pull-down), output (push-pull / open-drain / open-drain + pull-up), PWM, ADC, DAC. A device refuses a mode it can't do.
- **Interrupts** (`sys_io_intr_cfg_t`, 10 bytes): `{u8 mode, u8 debounce, sys_io_adc_window_t adc}`. Modes: disable, rising / falling / both edges, ADC window outside / inside. `adc` = `{u16 threshold_up_mV, u16 threshold_down_mV, u16 hysteresis_mV, u16 event_counter_threshold}`, read only by the window modes. The config only arms the pin.
- The device does the checks: `ERR_DEV_SUSPENDED` on writes while suspended, `ERR_DEV_BAD_PIN`, `ERR_DEV_RANGE` (value the chip would reject, never clamped).

## Errors (`ERR_MOD_SYS_IO` = 7, `sys_io.h`)

What a device with pins reports besides the shared `ERR_DEV_*` (BAD_PIN, PIN_LOCKED, RANGE, SUSPENDED): `ERR_IO_PIN_IN_USE {id, pin, mode}` (SET_MODE of a configured pin), `PIN_UNCONFIGURED {id, pin}`, `WRONG_MODE {id, pin, mode}` (the op doesn't fit the pin's mode), `MODE_UNSUPPORTED {id, pin, mode}`, `FEATURE_UNSUPPORTED {id, pin}`, `PWM_CHANNELS_EXHAUSTED {id, pin, channels}`, `PWM_TIMERS_EXHAUSTED {id, pin, timers, frequency_Hz}`.

## Pins contract (`0x02xx`, `sys_pins.h`)

Which pins a device has and which are locked. A pin another device depends on (PCA9685's OE, a chip RST) is locked by that device; a locked pin refuses the IO writes (`ERR_DEV_PIN_LOCKED`), reads stay allowed, and the owner drives it with SET_LOCKED_LEVEL. Masks are u64 LE, bit n = pin n (at most 64 pins).

| Op | Wrapper | Args | Answer |
|---|---|---|---|
| `0x0200` AVAILABLE | `sys_pins_available(dev, &mask)` | – | u64: the pins the device has |
| `0x0201` LOCKED | `sys_pins_locked(dev, &mask)` | – | u64: the locked ones |
| `0x0202` LOCK | `sys_pins_lock(p)` | `{u8 pin}` | – (`ERR_DEV_PIN_LOCKED` if already locked) |
| `0x0203` UNLOCK | `sys_pins_unlock(p)` | `{u8 pin}` | – (an unlocked pin stays unlocked) |
| `0x0204` SET_LOCKED_LEVEL | `sys_pins_set_locked_level(p, level)` | `{u8 pin, u8 level}` | – (while suspended: `ERR_DEV_SUSPENDED`) |

- A device keeps one `sys_pins_state_t` (`sys_pins_state_init(&s, count)`), calls `sys_pins_state_check_free` before every write (`check` for reads) and answers the PINS ops: AVAILABLE, LOCKED, LOCK and UNLOCK only read or change that state, so `SYS_PINS_STATE_HANDLERS(ctx_t)` writes them (struct with `base` and `pins` members) and `SYS_PINS_STATE_ENTRIES` lists their table rows; SET_LOCKED_LEVEL writes the pin and stays the device's own.
- "Reserved" pins are locked pins. A device reports every pin the chip has in AVAILABLE and nothing is reserved by the driver; whoever uses a pin for something else (a bridge driver on PWM channels, a chip RST) locks it, and LOCKED says what is taken. Free pins = `AVAILABLE & ~LOCKED`. Which pins get locked is board configuration, not driver code.
- A lock is bookkeeping, not an identity: whoever sends LOCK / UNLOCK is trusted to be the owner. An owner id in LOCK would close that; not needed yet.
- Taking a pin: `sys_io_set_mode`, then `sys_pins_lock`, then `sys_pins_set_locked_level` (PCA9685 OE does this); release: drive safe, `sys_pins_unlock`.

## Not yet

- **Pin events:** the IO events table (`SYS_IO_EVENTS`: RISING / FALLING / ADC_OUTSIDE / ADC_INSIDE = `0x0100`–`0x0103`, flags 0, SYS_EVENT.MD) is published by GPIO ESP (CONFIGURE_INTR, DEVICE_GPIO_ESP.MD); the expanders with interrupt pins (TCA6424A, ADS7128) are not ported.
