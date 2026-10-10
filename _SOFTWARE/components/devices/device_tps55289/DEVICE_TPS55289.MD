# TPS55289 — buck-boost regulator

Type `0x0043` (u16; answered by INFO). On the runIT board: two rails, bus 0, addresses 0x74 (rail A) and 0x75 (rail B), 10 mOhm sense resistor (`TPS55289_SHUNT_MOHM`). Port of `legacy/components/devices/device_tps55289`.

## Create

The type registers itself (`SYS_DEVICE_TYPE_DEFINE`, `SYS_DEVICE_TYPE_TPS55289`); the header exports only the create struct and the limits. CREATE:

`[05][id u16][00 00][43 00][bus][addr][intr: u16 device, u8 pin][en: u16 device, u8 pin]`

1. A one-shot `esp_timer` for the rise steps.
2. EN (if wired): output, locked on its device (PINS contract), driven high — the chip's I2C is down while EN is low, so this comes before the probe; 10 ms wake-up.
3. I2C probe.
4. FB/INT (if wired): input with pull-up, subscribed inline (`IO_EV_FALLING` of that pin → our ON_EVENT), armed with CONFIGURE_INTR falling edge, locked.
5. Defaults written with the output off (5 V, 200 mA, slowest slew, every fault indicated on FB/INT), then EN low: the rail stays off until SET_ENABLE.

The registry allocates the struct and adds the instance; on a failure it calls `uninstall()` (everything built is released) and wraps the error in `ERR_DEV_INSTALL{id, 0x0043}`.

| Field | Type | Notes |
|---|---|---|
| `i2c_bus` | u8 | 0 or 1, else `ERR_DEV_RANGE` |
| `i2c_addr` | u8 | 0x74 / 0x75 (MODE pin), else `ERR_DEV_RANGE` |
| `intr_pin` | `sys_io_pin_t` | `SYS_IO_PIN_NONE` = faults not reported. Its device must answer SET_MODE, CONFIGURE_INTR, the PINS ops and publish `IO_EV_FALLING` |
| `en_pin` | `sys_io_pin_t` | `SYS_IO_PIN_NONE` = no hardware switch (only the MODE register's OE bit). Its device must answer SET_MODE and the PINS ops |

Both pins' devices must exist (`sys_device_use`: `ERR_DEV_NOT_FOUND`); they can't be removed while the TPS55289 exists (`ERR_DEV_IN_USE`).

## Ops

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | 5 V / 200 mA wanted, rail off |
| `0x0002` SUSPEND | – | – | Output off, then EN low (EN goes low even if the I2C write fails); the wanted state is kept |
| `0x0003` RESUME | – | – | An enabled rail: EN high and every setting again (a rise). A failing resume turns the rail off and stays suspended |
| `0x0004` REMOVE | – | – | Generic (registry). `uninstall()`: timer stopped / deleted, output off, I2C released, FB/INT unsubscribed / unlocked / reset, EN low / unlocked / reset |
| `0x0005` INFO | – | `{u16 0x0043, u8 suspended}` | Generic |
| `0x0006` OPS | – | `u16[]` | Generic |
| `0x0500` SET_ENABLE | `{u8 enable}` | – | On: EN high, all settings rewritten (the chip forgot them), a rise starts; **returns at once**, `VREG_EV_READY` when settled. Already on: nothing. Off: rise stopped, output off, EN low |
| `0x0501` SET_VOLTAGE | `{u32 mV}` | – | 3000..20000. Higher on a running rail: a rise (returns at once, `VREG_EV_READY`); during a rise the ramp follows the new target. Lower or output off: written at once |
| `0x0502` SET_CURRENT | `{u32 mA}` | – | 200..5500; written raised to the 500 mA floor. During a rise only stored: the end of the rise writes it |
| `0x0300` ON_EVENT | `sys_event_t` | – | FB/INT fell: reads STATUS and publishes `VREG_EV_OVP` / `OCP` / `SCP` (SYS_POWER.MD), caused by the pin's event; OCP during a rise is the inrush and is not published |
| `0xFF00` RISE_STEP | – | – | Own op, sent by the rise timer through `sys_device_call`. Without a rise in progress (or while suspended) it does nothing; a failing step ends the rise where it is (soft-rise limit kept) and the timer reports the error |

Every write is refused while suspended (`ERR_DEV_SUSPENDED`). While the rail is off (EN low) SET_VOLTAGE / SET_CURRENT only store the wanted value.

## Behaviour kept from legacy

- **Soft rise:** every rise (enable, higher voltage) runs at 1000 mA (`TPS_SOFT_RISE_MA`), steps 1 V every 50 ms from 1 V, and applies the wanted limit after a 1 s settle window (`TPS_RAMP_SETTLE_MS`). STATUS is read at its end to clear the OCP the inrush latched.

## Rise (asynchronous, 2026-10-10)

The rise is a small state machine (`IDLE` → `RAMP` → `SETTLE` → `IDLE`) driven by a one-shot `esp_timer`. Each timer expiry calls `sys_device_call(id, 0xFF00)`, so every step is a short op under the registry lock (one or two I2C writes) instead of one op holding the lock for the whole second. Starting a rise sets the soft-rise limit and writes the first step at once; RAMP writes the next whole volt every 50 ms until the target; SETTLE waits out the rest of the 1 s window, then writes the wanted limit, reads STATUS and publishes `VREG_EV_READY` (value = mV, flags 0: code flow, e.g. a VM program waiting for its rail). SET_ENABLE off, SUSPEND and RESET stop the rise (timer stopped, state IDLE). While the state is not IDLE, ON_EVENT doesn't publish OCP (the legacy `rising` flag, back now that the lock is released between steps).
- **Current floor:** limits below 500 mA (5 mV over the shunt) are written as 500 mA: the chip's offset made lower settings flag OCP with no load.
- **External feedback** (`VOUT_FS` bit 7) is refused: `ERR_POWER_EXT_FEEDBACK`.

## Changes from legacy

- The rise runs from a timer (above): SET_ENABLE / SET_VOLTAGE return at once instead of after ≥ 1 s, and `VREG_EV_READY` is new.
- SET_ENABLE on an enabled rail does nothing (legacy re-applied everything, which dropped the output to the 1 V ramp start).
- A voltage the INTFB ratio can't reach is refused (`ERR_DEV_RANGE`) instead of clamped to the 11-bit reference. With the reset default ratio (INTFB 3) the range is 800..21270 mV, so the whole 3..20 V range fits.
- SET_VOLTAGE on a powered but disabled rail (no EN pin) just writes the reference: no ramp, no settle.
- REMOVE turns the output off over I2C before releasing the pins.
- The `is_current_limit_enabled` field is gone (it was always on).

## Open

- **EN wake-up:** turning the rail on still waits 10 ms inside the op (the chip's I2C start-up after EN), the one bounded wait SYS_DEVICE.MD allows.
- A rise step that was already waiting for the lock when the device is removed answers `ERR_DEV_NOT_FOUND`, reported by the timer (noise only).
- **FB/INT:** works on a GPIO ESP pin (CONFIGURE_INTR falling edge, debounce off). Flow: ISR → event task → inline listener → `sys_device_call(ON_EVENT)` (waits for the registry lock, so never during a rise) → STATUS read → `VREG_EV_*`. A fault that stays asserted keeps FB/INT low: no new edge until it clears. A pin on a device without CONFIGURE_INTR fails the create with `ERR_DEV_UNKNOWN_OP`.
- Not run on the board. The board-level blocker from legacy stage 2 (a rail above ~5 V power-cycling the board on PC USB, PROGRESS.md) is still open.
