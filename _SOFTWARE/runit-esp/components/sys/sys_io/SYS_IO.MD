# sys_io — the IO contract

Not a module with state: a set of device ops (contract `SYS_DEVICE_CONTRACT_IO` = `0x01`, ops `0x01xx`) that mean the same on every device that has them (PWM expander, GPIO expander, the ESP's own pins). A device lists the IO ops it can do in its op table (SYS_DEVICE.MD); an op it doesn't have answers `ERR_DEV_UNKNOWN_OP`. The wrappers in `sys_io.c` only build the `sys_device_call` (the setters `sys_device_write`, the getters `sys_device_read`, which also checks the answer's size), so an internal call and a remote command run the same code.

| File | Contents |
|---|---|
| `sys_io.h` | The `IO_OPERATIONS` table (op IDs, handler / entry macros), arg structs, the read answer helpers `sys_io_reply_level` / `sys_io_reply_voltage`, `sys_io_mode_e`, `sys_io_intr_mode_e` (machine-read `//#ref-enum` annotations), `sys_io_pin_t`, wrappers |
| `errors/sys_io_errors.h` | `ERR_SYS_IO_MAP`, the module's error tags (included by `sys_io.h`) |
| `errors/errors_sys_io.json` | The error messages |
| `sys_io.c` | The wrappers |
| `sys_pins.h` / `sys_pins.c` | The PINS contract (below): table, pin-state helper for devices, wrappers, a device's own pins |
| `contract_io.json`, `contract_pins.json` | The descriptors of the two contracts (checked against the tables by `templates/sync.sh`) |
| `AGENTS.md` | Rules for changing this folder |


## Pins

`sys_io_pin_t {u16 device, u8 pin}` (3 bytes, also the field of a create struct). An unused pin is `SYS_IO_PIN_NONE` (device `SYS_IO_PIN_NONE_DEVICE` = `0xFFFF`, pin `SYS_IO_PIN_NONE_INDEX` = `0xFF`, published as `io_pin_none`): a zero-filled field is device 0 / pin 0, a real pin. `sys_io_pin_valid(p)`.

## Ops

`[05][dir][id u16][op u16][args]` (SYS_DEVICE.MD); payloads are packed little-endian structs. The contract is the table `IO_OPERATIONS(X, P)` (`X(P, NAME, number, key_t, value_t)`, prefix `IO`): `IO_OP_<NAME>`, `IO_key_<NAME>_t` / `IO_value_<NAME>_t`, `OP_READ(IO, NAME)` / `OP_WRITE(IO, NAME)` and `OP_ENTRY(IO, NAME)` / `OP_ENTRY_R` / `OP_ENTRY_W` all come from it (SYS_DEVICE.MD, "Contract tables"). An RW op's read takes the pin and answers what its write takes, the pin included; a device may have one side only (OPS says which).

| Op | Side | Wrapper | Write args / read answer |
|---|---|---|---|
| `0x0100` LEVEL | RW | `sys_io_set_level(p, level)`, `sys_io_get_level(p, &level)` | `{u8 pin, u8 level}`; read key `{u8 pin}` |
| `0x0101` TOGGLE | W | `sys_io_toggle(p)` | `{u8 pin}` |
| `0x0103` PWM_DUTY | W | `sys_io_set_pwm_duty(p, duty)` | `{u8 pin, u32 duty}` |
| `0x0104` PWM_FREQ | W | `sys_io_set_pwm_frequency(p, Hz)` | `{u8 pin, u32 frequency_Hz}` |
| `0x0105` MODE | W | `sys_io_set_mode(p, mode)` | `{u8 pin, u8 mode}` (`sys_io_mode_e`) |
| `0x0106` RESET | W | `sys_io_reset(p)` | `{u8 pin}` (pin back to its power-on state) |
| `0x0107` INTR | W | `sys_io_configure_intr(p, &cfg)` | `{u8 pin, u8 mode, u8 debounce, u16 threshold_up_mV, u16 threshold_down_mV, u16 hysteresis_mV, u16 event_counter_threshold}` (11 B: the pin, then `sys_io_intr_cfg_t`) |
| `0x0109` VOLTAGE | RW | `sys_io_set_voltage(p, mV)`, `sys_io_get_voltage(p, &mV)` | `{u8 pin, i32 voltage_mV}` (signed: a negative reading is reported, not wrapped); read key `{u8 pin}` |

Example: the level of pin 4 of device 3, `[05][00][03 00][00 01][04]` → `[04][level]`.

- **Duty** is `0..IO_PWM_DUTY_MAX` (4095, 12-bit); a device with another resolution scales or rejects with `ERR_DEV_RANGE`.
- **Frequency:** a device with one frequency for all pins (PCA9685) ignores `pin`.
- **Modes** (`sys_io_mode_e`): input (plain / pull-up / pull-down), output (push-pull / open-drain / open-drain + pull-up), PWM, ADC, DAC. A device refuses a mode it can't do.
- **Interrupts** (`sys_io_intr_cfg_t`, 10 bytes): `{u8 mode, u8 debounce, sys_io_adc_window_t adc}`. Modes: disable, rising / falling / both edges, ADC window outside / inside. `adc` = `{u16 threshold_up_mV, u16 threshold_down_mV, u16 hysteresis_mV, u16 event_counter_threshold}`, read only by the window modes. The config only arms the pin.
- The registry refuses writes while the device is suspended (`ERR_DEV_SUSPENDED`, SYS_DEVICE.MD); the device does the other checks: `ERR_DEV_BAD_PIN`, `ERR_DEV_RANGE` (value the chip would reject, never clamped).

## Errors (`ERR_MOD_SYS_IO` = 7, `errors/sys_io_errors.h`)

What a device with pins reports besides the shared `ERR_DEV_*` (BAD_PIN, PIN_LOCKED, RANGE, SUSPENDED): `ERR_IO_PIN_IN_USE {id, pin, mode}` (MODE of a configured pin), `PIN_UNCONFIGURED {id, pin}`, `WRONG_MODE {id, pin, mode}` (the op doesn't fit the pin's mode), `MODE_UNSUPPORTED {id, pin, mode}`, `FEATURE_UNSUPPORTED {id, pin}`, `PWM_CHANNELS_EXHAUSTED {id, pin, channels}`, `PWM_TIMERS_EXHAUSTED {id, pin, timers, frequency_Hz}`, `BAD_READING {id, pin}` (an ADC did not return the channel asked for).

## A device's own pins (`sys_pins.h`)

A driver keeps one `sys_pins_owned_t` per pin it uses (zeroed = unused) and lets the shared functions do the take, drive and release:

- `sys_pins_take_output(dev, p, pin, level)`: use the pin's device, output mode, level, lock.
- `sys_pins_take_input(dev, p, pin, event, mode)`: use, input, subscribe `event` to the owner's ON_EVENT (inline; `0` = none), interrupt `mode`, lock.
- `sys_pins_drive(p, level)`: drive a pin the owner locked; a pin that is not locked is left alone, so teardown can call it without checking.
- `sys_pins_release(p)`: unsubscribe, unlock, reset the mode. Set `p->keep = true` after the take to leave an output as it is.

An invalid pin (`SYS_IO_PIN_NONE`) is stored as NONE and does nothing, so an optional pin needs no check at the call site. Teardown: drive the level the chip needs, then release.

## Pins contract (`0x02xx`, `sys_pins.h`)

Which pins a device has and which are locked. A pin another device depends on (PCA9685's OE, a chip RST) is locked by that device; a locked pin refuses the IO writes (`ERR_DEV_PIN_LOCKED`), reads stay allowed, and the owner drives it with LOCKED_LEVEL. Masks are u64 LE, bit n = pin n (at most 64 pins).

| Op | Side | Wrapper | Write args / read answer |
|---|---|---|---|
| `0x0200` AVAILABLE | R | `sys_pins_available(dev, &mask)` | → u64: the pins the device has |
| `0x0201` LOCKED | R | `sys_pins_locked(dev, &mask)` | → u64: the locked ones |
| `0x0202` LOCK | W | `sys_pins_lock(p)` | `{u8 pin}` (`ERR_DEV_PIN_LOCKED` if already locked) |
| `0x0203` UNLOCK | W | `sys_pins_unlock(p)` | `{u8 pin}` (an unlocked pin stays unlocked) |
| `0x0204` LOCKED_LEVEL | W | `sys_pins_set_locked_level(p, level)` | `{u8 pin, u8 level}` (the owner's op; a write, so refused while the device is suspended, `ERR_DEV_SUSPENDED`: the sweeps suspend the owner before the pin's device and resume it after) |

- A device keeps one `sys_pins_state_t` (`sys_pins_state_init(&s, count)`), calls `sys_pins_state_check_free` before every write (`check` for reads) and answers the PINS ops: AVAILABLE, LOCKED, LOCK and UNLOCK only read or change that state, so `SYS_PINS_STATE_HANDLERS(ctx_t)` writes them (struct with `base` and `pins` members) and `SYS_PINS_STATE_ENTRIES` lists their table rows; LOCKED_LEVEL writes the pin and stays the device's own.
- "Reserved" pins are locked pins. A device reports every pin the chip has in AVAILABLE and nothing is reserved by the driver; whoever uses a pin for something else (a bridge driver on PWM channels, a chip RST) locks it, and LOCKED says what is taken. Free pins = `AVAILABLE & ~LOCKED`. Which pins get locked is board configuration, not driver code.
- A lock is bookkeeping, not an identity: whoever sends LOCK / UNLOCK is trusted to be the owner. An owner ID in LOCK would close that; the contract has none.
- Taking a pin by hand: `sys_io_set_mode`, then `sys_pins_lock`, then `sys_pins_set_locked_level`; release: drive safe, `sys_pins_unlock`. A driver's own output pin uses `sys_pins_take_output` instead, which does the first, a level and the lock (PCA9685 OE, TPS55289 EN).
- Pins of a device the caller does not know (a bridge driver's IN / EN / IPROPI / VREF pins can sit on the ESP, the PCA9685, the TCA6424A or the DAC53202): `sys_io_prepare(p, mode)` is MODE that accepts a device without MODE (fixed-function pins: PCA9685 PWM channels, DAC outputs); `sys_io_prepare_input(p)` makes a pin an input with the pull-up where the device has one, else a plain input (the TCA6424A has no pull-ups: the board has external ones); `sys_io_release(p)` is RESET with the same tolerance. Any other failure is returned.

## Events

The IO events (`IO_EVENTS`: RISING / FALLING / ADC_OUTSIDE / ADC_INSIDE = `0x0100`–`0x0103`, flags 0; the table and delivery are in SYS_EVENT.MD) are raised for a pin armed with INTR, with channel = pin:

- **ESP32-S3 GPIO** (`device_esp32s3_gpio`): RISING / FALLING from its pin ISR; ADC_OUTSIDE / ADC_INSIDE from its ADC task, deferred.
- **TCA6424A** (`device_tca6424a`): its ON_EVENT runs on the INT pin's falling edge, reads the input port and publishes the edges of the pins armed with an edge mode. Nothing while suspended.
- **ADS7128** (`device_ads7128`): window crossings, read after its ALERT pin falls; the value is the voltage in mV.

A pin whose owner watches its edges takes it with `sys_pins_take_input` ("A device's own pins" above), which subscribes the owner's ON_EVENT.
