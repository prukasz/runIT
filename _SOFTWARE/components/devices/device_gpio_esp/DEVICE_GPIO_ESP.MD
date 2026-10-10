# GPIO ESP — the ESP32-S3's own pins

Type `0x0040` (u16; answered by INFO). Ported from `legacy/components/devices/device_gpio_esp` to the op-table shape (SYS_DEVICE.MD). One instance only: the pins are a hardware singleton, a second CREATE answers `ERR_DEV_ID_TAKEN` with the id of the first. By convention it is device 0 and created first, so every other device may use its pins (any ID works: the registry tracks who uses whom, SYS_DEVICE.MD).

| File | Contents |
|---|---|
| `device_gpio_esp.h` / `.c` | Create struct (none); ops, pin setup, the type (`SYS_DEVICE_TYPE_DEFINE`, `SYS_DEVICE_TYPE_GPIO_ESP`) |
| `gpio_esp_pin.h` | The pin object (`esp_pin_obj_t`), the static pin pool, `gpio_mutex` |
| `esp_pwm.c` / `.h` | PWM through LEDC: channel per pin, timer per frequency |
| `esp_adc_config.c` / `.h` | ADC1 background task (continuous mode, IIR filter, calibrated mV) |
| `Kconfig` | `DEVICE_GPIO_ESP_PWM_DEFAULT_FREQ_HZ` (1000), `_PWM_TIMER_MASK` (0xF), `_PWM_CHANNEL_MASK` (0xFF), `_DEBOUNCE_MS` (50) |

## Create

`[05][id u16][00 00][40 00]`: no create struct. It installs the GPIO ISR service (once per boot; REMOVE leaves it), starts the ADC task (idle until a pin is set to ADC); the registry adds the instance. The pin set is `SOC_GPIO_VALID_GPIO_MASK` (GPIO 0–21 and 26–48; 22–25 don't exist on the S3).

## Ops

`[0x05][id u16][op u16][args]`. Pin ≥ 49 or one of the missing GPIOs → `ERR_DEV_BAD_PIN`. A write while suspended → `ERR_DEV_SUSPENDED`; a write to a locked pin → `ERR_DEV_PIN_LOCKED`. Reads are allowed in both cases.

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | Releases every **unlocked** configured pin (a locked pin is left to its owner) |
| `0x0002` SUSPEND | – | – | Unlocked outputs made safe, state kept (below) |
| `0x0003` RESUME | – | – | Brings it back; a pin that fails stays marked and the device stays suspended |
| `0x0004` REMOVE | – | – | Generic (registry). `uninstall()` releases every pin (locked too) |
| `0x0005` INFO | – | `{u16 0x0040, u8 suspended}` | |
| `0x0006` OPS | – | `u16[]` | The table below |
| `0x0100` SET_LEVEL | `{u8 pin, u8 level}` | – | Push-pull / open-drain / open-drain + pull-up pins only (`ERR_IO_WRONG_MODE`) |
| `0x0101` TOGGLE | `{u8 pin}` | – | Same |
| `0x0102` GET_LEVEL | `{u8 pin}` | u8 | Not in ADC or PWM mode |
| `0x0103` SET_PWM_DUTY | `{u8 pin, u32 duty}` | – | 0..4095, else `ERR_DEV_RANGE`; 4095 = always on. The first write without a frequency binds a timer at the default frequency |
| `0x0104` SET_PWM_FREQ | `{u8 pin, u32 Hz}` | – | 5 Hz .. 5 MHz, else `ERR_DEV_RANGE` |
| `0x0105` SET_MODE | `{u8 pin, u8 mode}` | – | See modes |
| `0x0106` RESET | `{u8 pin}` | – | The pin back to unconfigured (disarms it too) |
| `0x0107` CONFIGURE_INTR | `{u8 pin, sys_io_intr_cfg_t}` | – | Arms / disarms the pin's events (below) |
| `0x0108` GET_VOLTAGE | `{u8 pin}` | i32 mV | ADC mode only (`ERR_IO_FEATURE_UNSUPPORTED` otherwise) |
| `0x0200`–`0x0204` | | | PINS contract (SYS_IO.MD): AVAILABLE, LOCKED, LOCK, UNLOCK, SET_LOCKED_LEVEL |

Not answered (→ `ERR_DEV_UNKNOWN_OP`): `0x0109` SET_VOLTAGE (a DAC device does that).

### Modes (SET_MODE)

Input, input + pull-up, input + pull-down, output push-pull, output open-drain, output open-drain + pull-up, PWM, ADC. DAC → `ERR_IO_MODE_UNSUPPORTED`. A pin that is already configured → `ERR_IO_PIN_IN_USE` (RESET it first).
- **PWM:** needs an output-capable pin and a free LEDC channel (`ERR_IO_PWM_CHANNELS_EXHAUSTED`). The pin is held low until the first frequency or duty write.
- **ADC:** ADC1 pins only (GPIO 1–10), else `ERR_IO_FEATURE_UNSUPPORTED`. Readings are averaged by the background task (20 kHz, 10 ms loop, IIR α 0.1) and calibrated; GET_VOLTAGE returns the last one.

### Pin events (CONFIGURE_INTR)

The pin must be configured first (`ERR_IO_PIN_UNCONFIGURED`). Events are the IO contract's (`SYS_IO_EVENTS`, SYS_EVENT.MD): source = this device, channel = pin, flags 0. Re-arming replaces the old config; mode 0 disarms. Mode > 5 or debounce > 1 → `ERR_CMD_BAD_ARG`. A configuration write: refused while suspended or on a pin locked by another device (TPS55289 arms its FB/INT pin first, then locks it).

| Pin mode | Interrupt modes | Source | Event, value |
|---|---|---|---|
| Input (any pull), output push-pull / open-drain | RISING, FALLING, BOTH | GPIO ISR | `IO_EV_RISING` / `IO_EV_FALLING`, value = level |
| ADC | RISING (≥ up), FALLING (≤ down), WINDOW_OUTSIDE, WINDOW_INSIDE | ADC task, every reading | `IO_EV_RISING` / `FALLING` / `ADC_OUTSIDE` / `ADC_INSIDE`, value = mV |
| PWM | – | | `ERR_IO_WRONG_MODE` |

- **Digital:** a one-edge mode publishes its own edge (the level read in the ISR may already have bounced back); BOTH publishes by the level read in the ISR. `debounce = 1` drops edges within `CONFIG_DEVICE_GPIO_ESP_DEBOUNCE_MS` of the last published one (switches); `debounce = 0` publishes every edge (chip alert lines: a dropped edge on a latched line is never followed by another). The ISR service runs without `ESP_INTR_FLAG_IRAM`, so the ISR may call flash code; `sys_event_publish` defers the whole delivery to the event task (an ISR drop is counted and reported there). An output sees its own edges (the pin is INPUT_OUTPUT), which the test uses.
- **ADC:** window modes need down ≤ up (`ERR_DEV_RANGE`); BOTH → `ERR_IO_FEATURE_UNSUPPORTED`, and the window modes on a digital pin too. An event fires once the condition held for `event_counter_threshold` readings in a row (0 = 1), then stays quiet until the reading is back past the hysteresis (OUTSIDE: inside the window by `hysteresis_mV`; INSIDE: outside by it; RISING: below up − hyst; FALLING: above down + hyst). Readings come every ~10 ms. The events are collected under `gpio_mutex` and published after it is released: an inline listener may call back into this device.
- The filter is seeded with the first average, so a FALLING / window alert armed right after SET_MODE doesn't fire on the climb from 0 mV.

### PWM pools

Two pools shared by every PWM pin, limited by the Kconfig masks (clear the bits another LEDC user needs): **channels**, one per PWM pin, and **timers**, one per distinct frequency. Pins at the same frequency share a timer; with none free, a new frequency answers `ERR_IO_PWM_TIMERS_EXHAUSTED` and the pin keeps its old one. Above ~19.5 kHz the duty has fewer than 4096 real steps (steps = 80 MHz / frequency). The duty is scaled to each timer's resolution.

### Suspend

The fault safe state of the pins that belong to the user: PWM duty 0, push-pull outputs low, open-drain outputs released. The values are kept and RESUME restores them. **Locked pins are skipped**: their owner makes them safe in its own SUSPEND (the sweep goes high ID to low, so the owner goes first), and the owner can still drive its pin with SET_LOCKED_LEVEL while this device is suspended.

## Differences from the legacy device

- Duty scale 0..4095 (the IO contract's `IO_PWM_DUTY_MAX`), not 0..4096.
- Open-drain + pull-up is a real mode (the legacy table listed it but refused it).
- Interrupts: ported (2026-10-10). Changes: a one-edge mode publishes its own edge instead of the level read in the ISR; `event_counter_threshold` is honoured (legacy ignored it); BOTH on an ADC pin is refused (legacy treated it as OUTSIDE); the ADC events are published outside `gpio_mutex`; the ISR handler is removed when the pin is reset.
- IO errors are `ERR_IO_*` of module `ERR_MOD_SYS_IO` (7) in `sys_io.h`; the ones shared by all devices stay `ERR_DEV_*`.
- Locks are the PINS contract, per instance, instead of `sys_io_lock_pin`.

## Tested

Builds. `router_tests.py` `test_gpio` (create, singleton, INFO, OPS, AVAILABLE, modes, level, toggle, interrupts through a rule on the output's own edges, PWM ranges, locks, suspend / resume, reset, remove; uses GPIO 4 and 5) has not been run on a board. ADC (readings and thresholds) is not covered.
