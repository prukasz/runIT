# ADS7128 — 8-channel 12-bit ADC with a window comparator

Type `0x0047`. On the runIT board: bus 0, address 0x10, ALERT → ESP 42, **full scale 20000 mV** (the front end scales the inputs to 20 V). The channels are IO pins 0..7, ADC only.

## Create

`[05][id u16][00 00][47 00][bus][addr][alert: u16 device, u8 pin][full_scale_mV u16]` — `ads7128_cfg_t`.

| Field | Notes |
|---|---|
| `i2c_bus` | 0 or 1, else `ERR_DEV_RANGE` |
| `i2c_addr` | 0x10..0x17 (the resistor on the ADDR pin), else `ERR_DEV_RANGE`; probed |
| `intr_pin` | ALERT, open-drain, active low; `SYS_IO_PIN_NONE` = not wired (no alert events). Its device must exist (`sys_device_use`) and answer SET_MODE, CONFIGURE_INTR and the PINS ops |
| `full_scale_mV` | The input voltage that reads code 4095: AVDD (2350..5500) on a bare chip, 20000 on the board. At least 1 (`ERR_DEV_RANGE`). Every reading and threshold is scaled by it |

`create()` resets the chip (a 5 ms wait for it to answer), writes the defaults, then makes ALERT an input (`sys_io_prepare_input`: with the pull-up where the device has one), subscribes inline to its `IO_EV_FALLING`, arms the falling edge and locks the pin.

## Ops

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | Chip reset, every comparator off |
| `0x0002` SUSPEND | – | – | The sequencer stops (nothing converts, no alert); the configuration is kept; reads and writes refused until RESUME |
| `0x0003` RESUME | – | – | The configuration is replayed; a channel that was past its threshold is armed on its entry window again |
| `0x0108` IO GET_VOLTAGE | `{u8 pin}` | `i32` mV | Refused while suspended. A manual conversion, or the last result of the autonomous sequencer when any alert is armed. A manual read retries until the channel ID appended to the data matches (`ERR_IO_BAD_READING` after 3 tries) |
| `0x0105` IO SET_MODE | `{u8 pin, u8 mode}` | – | ADC only; another mode → `ERR_IO_MODE_UNSUPPORTED` |
| `0x0107` IO CONFIGURE_INTR | `{u8 pin, sys_io_intr_cfg_t}` | – | The window comparator: modes `ADC_WINDOW_OUTSIDE` / `INSIDE` (edge modes → `ERR_IO_FEATURE_UNSUPPORTED`, a mode above 5 → `ERR_CMD_BAD_ARG`); DISABLE turns the channel off |
| `0x0106` IO RESET | `{u8 pin}` | – | The channel's comparator off |
| `0x0300` ON_EVENT | `sys_event_t` | – | ALERT fell: reads the flags, publishes each new crossing |

Window: `threshold_up_mV` 0 = no upper limit; both thresholds 0..full scale and down ≤ up (else `ERR_DEV_RANGE`); `hysteresis_mV` up to 15 steps of 8 codes (`ERR_DEV_RANGE` above); `event_counter_threshold` 0..16 readings past the threshold before it triggers (the chip counts n + 1). In-band the chip flags HIGH_TH < code < LOW_TH, so the bounds swap registers (verified on the board 2026-09-24).

Pin ≥ 8 → `ERR_DEV_BAD_PIN`.

## Events

`IO_EV_ADC_OUTSIDE` / `IO_EV_ADC_INSIDE` (by the configured mode), source = this device, channel = the pin, value = the reading in mV, flags 0 (code flow: a VM block or a listener). One per genuine crossing: after it fires the chip is re-armed on an **exit watch** (the opposite condition, retreated by the hysteresis), and only its trip re-arms the entry window. The flags are latched and not self-clearing, and ALERT is edge-triggered here, so the handler loops until a read finds nothing pending.

## Changes from legacy

- Integer scaling (legacy: floats); `full_scale_mV` is u16 (legacy u32).
- Edge modes, thresholds, hysteresis and the event count are range-checked (legacy clamped silently).
- SUSPEND stops the sequencer (legacy did nothing); RESUME replays the configuration.
- The alert is an event of the IO contract instead of `sys_io_publish`; the device handles it as its own ON_EVENT.

## Tested

2026-10-11 on the runIT PCB (`scripts/board_tests.py`): create errors, eight readings, mode and interrupt errors, ranges, an outside and an inside window on channel 0 (ALERT released again, no error records), suspend / resume, resets. The events themselves are not seen by the script (flags 0: not on the events stream); a crossing was not provoked with a real signal.
