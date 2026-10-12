# INA3221 — three-channel power monitor

Type `0x0044` (u16; answered by INFO). On the runIT board: bus 0, 10 mOhm shunts (`SHUNT_MOHM`), channel map rail B = ch0, input = ch1, rail A = ch2, all three shunts reversed by design (`inverted_mask = 0x07`; HARDWARE.md). Port of `legacy/components/devices/device_ina3221` (2026-10-11). **Builds; not run on hardware yet** (the devkit has no INA3221).

## Create

`[05][id u16][00 00][44 00][bus][addr][crit: u16 device, u8 pin][warn: u16 device, u8 pin][inverted_mask]`

| Field | Type | Notes |
|---|---|---|
| `i2c_bus` | u8 | 0 or 1, else `ERR_DEV_RANGE` |
| `i2c_addr` | u8 | 0x40..0x43 (A0 strap), else `ERR_DEV_RANGE` |
| `crit_pin` | `sys_io_pin_t` | critical alert, open drain, active low; `SYS_IO_PIN_NONE` = not wired. Its device must answer SET_MODE, CONFIGURE_INTR, the PINS ops and publish `IO_EV_FALLING` / `IO_EV_RISING` |
| `warn_pin` | `sys_io_pin_t` | warning alert, same |
| `inverted_mask` | u8 | 0..7, else `ERR_DEV_RANGE`. Bit N: channel N's shunt is reversed: its current (and power) is reported negated |

Steps: pin devices declared (`sys_device_use`), I2C probe, each wired alert pin: input (`sys_io_prepare_input`: pull-up where the device has one), an inline subscription to every IO event of that pin (→ ON_EVENT), falling edge armed, locked. Then chip reset, both alert latches on, continuous bus + shunt measurement.

## Ops

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | Chip reset (limits back to defaults), reversed-shunt alerts disarmed (pins back to the falling edge), latches on, measuring |
| `0x0002` SUSPEND | – | – | Power-down (no conversions). Readings stay allowed: the last conversion |
| `0x0003` RESUME | – | – | Continuous measurement again |
| `0x0004..6, 7, 8` | generic | | REMOVE (`uninstall()`: subscriptions, pin locks, pins reset, I2C), INFO, OPS, error policy |
| `0x0400` GET_VOLTAGE | `{u8 channel}` | i32 mV | bus voltage, 8 mV LSB |
| `0x0401` GET_CURRENT | `{u8 channel}` | i32 mA | shunt voltage (40 uV LSB) / 10 mOhm; negated on a reversed shunt |
| `0x0402` SET_ALERT | `{u8 channel, u8 alert, i32 threshold_mA}` | – | `alert` 0 warning / 1 critical (`sys_power_alert_e`). Normal shunt: ±16380 mA (12-bit limit), else `ERR_DEV_RANGE`. Reversed shunt: see below. Refused while suspended |
| `0x0403` GET_POWER | `{u8 channel}` | i32 mW | `sys_power_mW(voltage, current)` (the chip has no power register) |
| `0x0300` ON_EVENT | event | – | an alert pin changed (below) |

Channel > 2: `ERR_POWER_BAD_CHANNEL{id, channel}`. Unknown alert: `ERR_POWER_ALERT_UNSUPPORTED`.

## Alerts → events

`PMON_EV_OCP_WARNING` / `PMON_EV_OCP_CRITICAL` (NOTIFY), source = this device, channel = the measured channel, value = mA.

- **Normal shunt:** the pin's falling edge → the mask register is read (clears the latched flags) → one event per flagged channel and alert (a failed current read still publishes, value 0). Reversed-shunt channels are skipped (their flag is set while under the threshold, their normal state).
- **Reversed shunt** (`inverted_mask`): the chip compares signed, so the limit is −threshold: the alert is active while the current is below the threshold and releases above it. That alert is made transparent (its latch off) and its pin moved to the rising edge (the release is the alert). One reversed channel per alert pin: a second one (or a pin not wired) answers `ERR_POWER_ALERT_UNSUPPORTED`. RESET disarms them.

## Changes from legacy

- New tree shape: create struct with `sys_io_pin_t` (u16 device), registry-allocated struct, `sys_event_subscribe` (inline, device target) instead of `sys_io_subscribe_pin`, PINS contract lock / unlock.
- GET_POWER added (contract op 0x03).
- Errors: `ERR_POWER_BAD_CHANNEL` / `ERR_POWER_ALERT_UNSUPPORTED` instead of `SE_CHECK_IN_RANGE` / `ERR_DEV_FEATURE_UNAVAILABLE`; a reversed-shunt alert on a pin already watching another channel answers `ERR_POWER_ALERT_UNSUPPORTED` (legacy: `ERR_IO_PIN_ALREADY_IN_USE`); a normal-shunt threshold outside the 12-bit limit is refused (legacy wrapped it).
- Current maths in integers (40 uV × raw / 10 mOhm) instead of float.
