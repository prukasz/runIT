# sys_io — the IO contract

Not a module with state: a set of device ops (contract `IO` = `0x01`, its module's number `MODULE_ID_IO`, ops `0x01xx`) that mean the same on every device that has pins: a PWM expander (PCA9685), a GPIO expander, the ESP32-S3's own pins. A device lists the IO ops it can do in its op table (SYS_DEVICE.MD, "A driver"); an op it doesn't have answers `ERR_DEV_UNKNOWN_OP`. A device's pin is (device ID, pin), and using it is a call of that device's op: the same one a command makes.

| File | Contents |
|---|---|
| `sys_io.h` | The caller's side: the table `IO_OPERATIONS`, the frames, `sys_io_mode_e` and `sys_io_intr_mode_e` (machine-read `//#ref-enum` annotations), the events `IO_EVENTS`, `sys_io_pin_t`, `SYS_IO_WRITE` / `SYS_IO_READ`, the wrappers |
| `sys_io.c` | The wrappers; claims the contract's ID (`SYS_DEVICE_CONTRACT_DEFINE(IO, 0x01)`) and the error module |
| `driver/sys_io_driver.h`, `.c` | A device with pins: the pin state it keeps (`sys_io_pins_t`), its checks, the lock ops |
| `errors/sys_io_errors.h`, `errors/errors_sys_io.json` | The module's error tags and their messages |
| `contract_io.json` | The contract's descriptor (checked against the table by `templates/sync.sh`) |
| `AGENTS.md` | Rules for changing this folder |

A device that uses another device's pin (a chip's RST, OE, EN, an alert line) takes it through `sys_io_claim` (`components/sys/sys_io_claim/`), a component of its own: it is the only part of IO that needs the EVENT contract.

## Pins

`sys_io_pin_t {u8 device, u8 pin}` (2 B), also the field of a create struct. An unused pin is `SYS_IO_PIN_NONE` (device `SYS_IO_PIN_NONE_DEVICE` = `SYS_DEVICE_ID_NONE` (`0xFF`), pin `SYS_IO_PIN_NONE_INDEX` = `0xFF`, published as `io_pin_none`): a zero-filled field is device 0 / pin 0, a real pin. `sys_io_pin_valid(p)`.

Which device a pin is on is the board's configuration, never a driver's: a driver asks whether the device has the op it needs (`sys_device_require_op`), not what type it is. A pin's index is the device's own numbering: on the ESP32-S3 it is the GPIO (pin 46 is GPIO 46).

## Ops

The contract is the table `IO_OPERATIONS(X, P)` (`X(P, NAME, n, key_t, value_t, flags)`): `IO_OP_<NAME>`, `IO_key_<NAME>_t` / `IO_value_<NAME>_t`, the handlers `OP_READ(IO, NAME)` / `OP_WRITE(IO, NAME)` and the rows `OP_ENTRY(IO, NAME)` / `OP_ENTRY_R` / `OP_ENTRY_W` all come from it (SYS_DEVICE.MD). The read of an RW op takes the pin and answers what its write takes, the pin included; a device may have one side only (OPS says which). Payloads are packed little-endian structs.

| Op | Side | Write args / read answer |
|---|---|---|
| `0x0100` LEVEL | RW | `{u8 pin, u8 level}`; read key `{u8 pin}` |
| `0x0101` TOGGLE | W | `{u8 pin}` |
| `0x0103` PWM_DUTY | W | `{u8 pin, u32 duty}` |
| `0x0104` PWM_FREQ | W | `{u8 pin, u32 frequency_Hz}` |
| `0x0105` MODE | W | `{u8 pin, u8 mode}` (`sys_io_mode_e`) |
| `0x0106` RESET | W | `{u8 pin}`: the pin back to its power-on state |
| `0x0107` INTR | W | `{u8 pin, sys_io_intr_cfg_t cfg}` (11 B) |
| `0x0109` VOLTAGE | RW | `{u8 pin, i32 voltage_mV}` (signed: a negative reading is reported, not wrapped); read key `{u8 pin}` |
| `0x010A` AVAILABLE | R | u64: the pins the device has |
| `0x010B` LOCKED | R | u64: the locked ones |
| `0x010C` LOCK | W | `{u8 pin}`; ALWAYS, LOCAL |
| `0x010D` UNLOCK | W | `{u8 pin}`; ALWAYS, LOCAL |

`0x02`, `0x08` and `0x0E` are unused: never assign them.

- **Duty** is `0..IO_PWM_DUTY_MAX` (4095, 12-bit); a device with another resolution scales, or refuses with `ERR_DEV_RANGE`.
- **Frequency**: a device with one frequency for all pins (PCA9685) ignores `pin`.
- **Modes** (`sys_io_mode_e`): input (plain, pull-up, pull-down), output (push-pull, open-drain, open-drain with pull-up), PWM, ADC, DAC. A device refuses a mode it can't do (`ERR_IO_MODE_UNSUPPORTED`).
- **Interrupts** (`sys_io_intr_cfg_t`, 10 B): `{u8 mode, u8 debounce, sys_io_adc_window_t adc}`. Modes: disable, rising / falling / both edges, ADC window outside / inside. `adc` = `{u16 threshold_up_mV, u16 threshold_down_mV, u16 hysteresis_mV, u16 event_counter_threshold}`, read by the threshold modes of an ADC pin. INTR only arms the pin; who gets its events is not in the contract.
- **Masks** are u64, bit n = pin n: a device has at most 64 pins.
- The registry refuses a write while the device is suspended (`ERR_DEV_SUSPENDED`, SYS_DEVICE.MD); the device does the rest: `ERR_DEV_BAD_PIN`, `ERR_DEV_RANGE` (a value the chip would refuse, never clamped).

## Calls

```c
ERR_TRY(SYS_IO_WRITE(pin, LEVEL, .level = 1));
ERR_TRY(SYS_IO_WRITE(pin, INTR, .cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE}));
io_voltage_args_t v; ERR_TRY(SYS_IO_READ(pin, VOLTAGE, &v));
```

`SYS_IO_WRITE(p, NAME, fields)` / `SYS_IO_READ(p, NAME, &answer)` are `SYS_DEVICE_SET` / `SYS_DEVICE_GET` on the pin's device with `.pin` filled in: the frame comes from the table and the compiler checks the field names. The wrappers in `sys_io.c` are only the ones with logic of their own:

| Wrapper | What it does |
|---|---|
| `sys_io_prepare(p, mode)` | MODE, and a device without MODE (fixed-function pins: PCA9685 channels, DAC outputs) is fine |
| `sys_io_prepare_input(p)` | An alert / interrupt line as an input: with the pull-up where the device has one, else plain (an expander without pull-ups: the board has an external one) |
| `sys_io_release(p)` | RESET, with the tolerance of `sys_io_prepare` |
| `sys_io_get_level(p, &level)` | LEVEL's read as a bool; allowed on a locked pin |
| `sys_io_get_voltage(p, &mV)` | VOLTAGE's read |

## A device with pins (`driver/sys_io_driver.h`)

A device keeps one `sys_io_pins_t`: which pins exist (`available`), which are locked (`locked`) and by whom (`owner[64]`). `sys_io_pins_init(&s, count)` makes pins `0..count-1` exist, none locked, every owner `SYS_DEVICE_ID_NONE`; a device whose pins are not contiguous sets `available` itself (the ESP32-S3 has no GPIO 22-25).

- Before every write of the contract the handler calls `sys_io_pins_check_free(&c->pins, c->id, pin)`, before a read `sys_io_pins_check`.
- AVAILABLE, LOCKED, LOCK and UNLOCK only read or change that state, so they are the same on every device: `SYS_IO_PINS_HANDLERS` writes them (the driver's struct has the members `id` and `pins`) and `SYS_IO_PINS_ENTRIES` lists their rows.
- Nothing is reserved by a driver: a device reports every pin it has in AVAILABLE, whoever uses a pin for something else locks it, and LOCKED says what is taken. Free pins: `AVAILABLE & ~LOCKED`.

### Locked pins and their owners

A pin another device depends on (PCA9685's OE, a chip's RST, an I2C bus's SDA) is locked by that device. A lock has an owner: the device whose op called LOCK (`sys_device_caller()`).

- `sys_io_pins_check_free` means "free, or mine": it passes a pin that is not locked and one the caller of the op locked. The owner writes its pin with the ordinary ops (LEVEL, PWM_DUTY, INTR, ...); the same write from anyone else (another device, the app, a VM program) is `ERR_DEV_PIN_LOCKED{id, pin, owner}`. There is no op for the owner alone.
- A pin with the owner `SYS_DEVICE_ID_NONE` is no device's (the chip's own peripheral): every write refuses it.
- LOCK of a locked pin is `ERR_DEV_PIN_LOCKED`, also for its owner: a lock is taken once. UNLOCK from anyone but the owner is `ERR_DEV_PIN_NOT_OWNED{id, pin, owner}`; UNLOCK of a pin that is not locked does nothing.
- LOCK and UNLOCK are LOCAL (a driver's C reaches them through `sys_io_claim`; the device router refuses them, `ERR_DEV_LOCAL_OP`) and ALWAYS (a suspended device's pins are still taken and given back).
- Reads stay allowed on a locked pin.

### The ESP32-S3's own pins

`device_esp32s3_gpio` (type `0x0040`, one per board, `runit-esp-rework/components/devices/device_esp32s3_gpio/`) is the one device whose driver locks pins itself: a pin a native peripheral holds (flash, PSRAM, the USB-Serial-JTAG; reserved in ESP-IDF, `esp_gpio_reserve`) is locked at its CREATE for no device (owner `SYS_DEVICE_ID_NONE`), and MODE refuses a pin reserved later with `ERR_DEV_PIN_LOCKED{owner SYS_DEVICE_ID_NONE}`. A peripheral that a device drives (the ESP32-S3's I2C controller, `device_esp32s3_i2c`) locks its pins for that device instead (`sys_io_claim_peripheral`). Its other rules (PWM pools, the ADC task, its safe state) are in the driver's comments and `esp32s3_gpio.h`.

## A device that uses another device's pin (`sys_io_claim`)

The driver keeps one `sys_io_claim_t` per pin it uses and takes it in its `create()`; the take declares the use of the pin's device (`sys_device_use`) and adds the pin as a resource of the owner, so the registry gives it back when the owner is removed or its `create()` fails. A driver never releases a pin itself.

| Take | What it does to the pin |
|---|---|
| `sys_io_claim(owner, p, pin, mode, event)` | `mode` (`sys_io_prepare`), not locked: the owner writes it with the IO ops (a PWM duty, a DAC voltage, an ADC pin's thresholds); `event` (0: none) reaches the owner's ON_EVENT |
| `sys_io_claim_output(owner, p, pin, level)` | Push-pull output at `level`, locked |
| `sys_io_claim_input(owner, p, pin, event, mode)` | Input (`sys_io_prepare_input`), `event` to the owner's ON_EVENT, interrupt `mode`, locked |
| `sys_io_claim_peripheral(owner, p, pin)` | Locked, its mode left to a peripheral of the pin's own chip |

- `sys_io_claim_drive(p, level)` writes LEVEL of a pin the owner locked and does nothing for one that is not locked, so an optional pin needs no check. Any other write of a taken pin is the IO op itself: `SYS_IO_WRITE(p->pin, INTR, ...)`.
- The release undoes what the take did: unsubscribe, UNLOCK, RESET (`sys_io_release`). Set `p->keep = true` after the take to leave an output as it is (PCA9685's OE stays at its outputs-off level).
- `SYS_IO_PIN_NONE` is stored and nothing is done, so an optional pin needs no check at the call site.
- Called from an op of the owner only (`create()`, `suspend()`, `resume()`, a timer's op, ON_EVENT): the pin's device lets its owner write because the owner is the caller.

## Events

`IO_EVENTS` (`SYS_EVENTS_DECLARE(IO)`, module `0x01`; the table and the delivery are in SYS_EVENT.MD), published by a device for a pin armed with INTR: source = the device, channel = the pin. Flags 0: an edge drives code (an alert pin, the VM) and is not shown by itself.

| Event | Code | Payload |
|---|---|---|
| `IO_EV_RISING` | `0x01` | none |
| `IO_EV_FALLING` | `0x02` | none |
| `IO_EV_ADC_OUTSIDE` | `0x03` | `i32 mV`: the reading |
| `IO_EV_ADC_INSIDE` | `0x04` | `i32 mV`: the reading |

The ESP32-S3 GPIO publishes RISING / FALLING of a digital pin from its pin ISR (deferred to the event task, as every publish from an ISR) and RISING / FALLING / ADC_OUTSIDE / ADC_INSIDE of an ADC pin's thresholds from its ADC task, with `SYS_EVENT_PUBLISH_DEFER`. A device that watches another device's pin takes it with `sys_io_claim_input`, which subscribes its ON_EVENT.

## Errors (module `0x01`, `ERR_MOD_SYS_IO` = `MODULE_ID_IO`)

What a device with pins reports besides the registry's `ERR_DEV_BAD_PIN`, `ERR_DEV_PIN_LOCKED`, `ERR_DEV_PIN_NOT_OWNED`, `ERR_DEV_RANGE`, `ERR_DEV_SUSPENDED`:

| Tag | When |
|---|---|
| `ERR_IO_PIN_IN_USE{id, pin, mode}` | MODE of a configured pin; `mode` is its current one |
| `ERR_IO_PIN_UNCONFIGURED{id, pin}` | The op needs MODE first |
| `ERR_IO_WRONG_MODE{id, pin, mode}` | The op doesn't fit the pin's mode |
| `ERR_IO_MODE_UNSUPPORTED{id, pin, mode}` | The pin or the device can't do the mode |
| `ERR_IO_FEATURE_UNSUPPORTED{id, pin}` | ADC on a pin without one, an interrupt mode the pin can't do |
| `ERR_IO_PWM_CHANNELS_EXHAUSTED{id, pin, channels}` | No PWM channel left for the pin |
| `ERR_IO_PWM_TIMERS_EXHAUSTED{id, pin, timers, frequency_Hz}` | A new frequency needs a timer and none is free |
| `ERR_IO_BAD_READING{id, pin}` | An ADC did not return the channel asked for |
