# TCA6424A — 24-bit GPIO expander

Type `0x0042` (u16; answered by INFO). On the runIT board: bus 0, address 0x23 (ADDR high), device ID 1 (`DEVICE_ID_TCA6424A`), INT → ESP 9, RST → ESP 8. Its pins carry the board's internal control: the LM73100 power-mux enables, the TPS55289 EN / INT, the DRV8962 sleep / fault, the INA3221 alerts, the AP33772S INT, the eFuse PG lines and the PCA9685 OE (HARDWARE.md §3). Port of `legacy/components/devices/device_tca6424a` (2026-10-11). **Builds; not run on hardware yet.**

## Create

`[05][id u16][00 00][42 00][bus][addr][intr: u16 device, u8 pin][rst: u16 device, u8 pin]`

| Field | Type | Notes |
|---|---|---|
| `i2c_bus` | u8 | 0 or 1, else `ERR_DEV_RANGE` |
| `i2c_addr` | u8 | 0x22 (ADDR low) / 0x23 (ADDR high), else `ERR_DEV_RANGE`; probed (`ERR_I2C_DEV_NOT_FOUND`) |
| `intr_pin` | `sys_io_pin_t` | INT, open drain, active low. `SYS_IO_PIN_NONE` = no pin events. Its device must answer SET_MODE, CONFIGURE_INTR, the PINS ops and publish `IO_EV_FALLING` |
| `rst_pin` | `sys_io_pin_t` | RESET, active low. `SYS_IO_PIN_NONE` = no hardware reset. Its device must answer SET_MODE, SET_LEVEL and the PINS ops |

Pin devices are declared (`sys_device_use`): they must exist and can't be removed while the expander does. Steps: I2C probe; RST (if wired) output, high, locked on its device; the start state written (every pin an input, latches low, no inversion); INT (if wired) input (`sys_io_prepare_input`: pull-up where the device has one), an inline subscription to `IO_EV_FALLING` of that pin → our ON_EVENT, falling edge armed, locked; the input port read as the base of edge detection. On a failure the registry calls `uninstall()` and wraps the error in `ERR_DEV_INSTALL{id, 0x0042}`.

## Ops

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | **Unlocked** pins back to power-on: input, latch low, interrupt disarmed. Locked pins (a rail's EN, a switch enable, PCA OE) keep their level; the chip itself is not reset |
| `0x0002` SUSPEND | – | – | RST low (the chip resets: every pin an input), or without RST every pin an input. Users of the pins are suspended before (newest first). Writes and reads refused until RESUME |
| `0x0003` RESUME | – | – | RST high, 10 ms, the cached registers written back (outputs, polarity, then direction), inputs read again. A failing resume leaves it suspended |
| `0x0004..8` | generic | | REMOVE (`uninstall()`: every pin an input unless suspended, I2C released, INT unsubscribed / unlocked / reset, RST unlocked / reset), INFO, OPS, error policy |
| `0x0100` SET_LEVEL | `{u8 pin, u8 level}` | – | Writes the output latch (takes effect when the pin is an output) |
| `0x0101` TOGGLE | `{u8 pin}` | – | From the cached latch |
| `0x0102` GET_LEVEL | `{u8 pin}` | u8 | Reads the input port (the pin's real level, outputs too); allowed on a locked pin, refused while suspended (the chip is in reset) |
| `0x0105` SET_MODE | `{u8 pin, u8 mode}` | – | `SYS_IO_MODE_INPUT` or `OUTPUT_PUSH_PULL`; anything else `ERR_IO_MODE_UNSUPPORTED`. A pin set to output drives its latch, which starts low |
| `0x0106` RESET | `{u8 pin}` | – | The pin back to power-on (input, latch low, interrupt disarmed) |
| `0x0107` CONFIGURE_INTR | `{u8 pin, cfg}` | – | Modes disable / rising / falling / both; the ADC window modes `ERR_IO_FEATURE_UNSUPPORTED`; `debounce` is ignored (the chip has no filter) |
| `0x0200..0203` | PINS | | AVAILABLE (`0x00FFFFFF`), LOCKED, LOCK, UNLOCK |
| `0x0204` SET_LOCKED_LEVEL | `{u8 pin, u8 level}` | – | The owner drives its locked pin |
| `0x0300` ON_EVENT | `sys_event_t` | – | INT fell (below) |

Pin ≥ 24: `ERR_DEV_BAD_PIN`. IO writes to a locked pin: `ERR_DEV_PIN_LOCKED`. Writes while suspended: `ERR_DEV_SUSPENDED`. Any other op: `ERR_DEV_UNKNOWN_OP`.

## Pin events

ON_EVENT (the INT pin fell): reads the input port (which releases INT), compares it with the last read, and publishes `IO_EV_RISING` (value 1) or `IO_EV_FALLING` (value 0) for every changed pin armed with CONFIGURE_INTR for that edge, source = this device, channel = the pin. While suspended it does nothing (the INT edge of the reset is not an input change). A pin that is an output is compared too, but the chip only raises INT for input pins, so an output's own edges are seen only when another pin's change reads the port.

## Changes from legacy

- The cfg has no `device_id`, pins are `sys_io_pin_t` (u16 device), the type is `0x0042` as u16; the contract is the op table (IO + PINS + ON_EVENT) instead of the `sys_io_contract_t` vtable.
- **PINS contract (new for this device):** pins can be locked by their users, which the board needs (rail EN, switch enables, PCA OE).
- **Latches start low** (legacy: the chip's 1): a pin set to output no longer shows a brief high, which would switch an LM73100 / rail enable on. `chip_restore` writes the outputs before the direction for the same reason. RESET's "power-on" latch (low) is now the same as the start state.
- **RESET leaves locked pins alone and does not pulse RST** (legacy pulsed RST and cleared all 24 pins, which would drop the rails and power-mux switches under their owners). RST is now used by SUSPEND / RESUME only.
- **CREATE releases RST before it probes the chip** (RST high, 10 ms, then the I2C probe). SUSPEND leaves RST low and the pad keeps it across an ESP restart, so with the probe first a restart after the safe state (STOP) found no chip (`ERR_I2C_DEV_NOT_FOUND`) and the whole `devices` boot step failed (found on the PCB 2026-10-11, `board_tests.py` now sends STOP before its restart check).
- **SUSPEND without RST** makes every pin an input (legacy did nothing); REMOVE does the same, so nothing stays driven.
- GET_LEVEL is refused while suspended (legacy read the chip in reset).
- The `in_reset` flag is gone: ON_EVENT is an op, so it waits for the registry lock that RESET / SUSPEND / RESUME hold, and answers nothing while suspended.
- Same as legacy, not decided: SET_MODE of a pin already configured succeeds (GPIO ESP answers `ERR_IO_PIN_IN_USE`) and the level of an input pin can be written (takes effect when it becomes an output). PROGRESS.md finding 8 asks for one rule across the IO devices.
