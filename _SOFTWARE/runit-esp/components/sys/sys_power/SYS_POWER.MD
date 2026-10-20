# sys_power — the power contracts

Not a module with state: sets of device ops that mean the same on every device that has them, like the IO contract (SYS_IO.MD). A device lists the ops it can do in its op table (SYS_DEVICE.MD); an op it doesn't have answers `ERR_DEV_UNKNOWN_OP`. The wrappers in `sys_power.c` only build the `sys_device_call` (the getters `sys_device_read`, which also checks the answer's size), so an internal call and a remote command run the same code.

| File | Contents |
|---|---|
| `sys_power.h` | Errors (`ERR_MOD_SYS_POWER` = 10), `sys_power_mW()` |
| `sys_power_monitor.h` | POWER_MONITOR: contract table, events, `sys_power_alert_e` (`//#ref-enum`), wrappers |
| `sys_power_vreg.h` | POWER_VREG: contract table, events, wrappers |
| `sys_power_supply.h` | POWER_SUPPLY: contract table, `sys_power_supply_source_e` (`//#ref-enum`), wrappers |
| `sys_power_usb_pd.h` | POWER_USB_PD: contract table, option and limits structs, wrappers |
| `sys_power.c` | The error module and the wrappers. `WHOLE_ARCHIVE`: a device raises `ERR_POWER_*` without calling a wrapper, and the tag registration must not be dropped |
| `contract_power_*.json` | The descriptors of the four contracts |
| `AGENTS.md` | Rules for changing this folder |

## POWER_MONITOR (`0x04xx`)

`[05][id u16][op u16][args]`; payloads are packed little-endian. Prefix `PMON_` (`PMON_OP_*`, `pmon_args_*_t`, `PMON_HANDLER`, `PMON_ENTRY`, `PMON_EV_*`).

| Op | Wrapper | Args | Answer |
|---|---|---|---|
| `0x0400` GET_VOLTAGE | `sys_power_monitor_get_voltage(dev, ch, &mV)` | `{u8 channel}` | i32 mV |
| `0x0401` GET_CURRENT | `sys_power_monitor_get_current(dev, ch, &mA)` | `{u8 channel}` | i32 mA |
| `0x0402` SET_ALERT | `sys_power_monitor_set_alert(dev, ch, alert, mA)` | `{u8 channel, u8 alert, i32 threshold_mA}` | – |
| `0x0403` GET_POWER | `sys_power_monitor_get_power(dev, ch, &mW)` | `{u8 channel}` | i32 mW |

- Readings are signed (a reversed shunt reports a negative current) and allowed while suspended.
- **GET_POWER**: a chip with a power register reads it; one without answers `sys_power_mW(mV, mA)` from its own two readings (`int64` product, so 20 V × 5.5 A doesn't overflow).
- **Alerts** (`sys_power_alert_e`): `OCP_WARNING` 0, `OCP_CRITICAL` 1. An armed alert is published as the event with the same n. A device that can't do the alert on that channel answers `ERR_POWER_ALERT_UNSUPPORTED`. The INA3221 publishes a reversed-shunt alert (transparent on the chip) once per excursion: after the event its limit moves 10 % lower and the threshold is armed again only when the current is back under that (`ALERT_HYSTERESIS_PCT`), so a current around the threshold is not an event per conversion.

| Event | ID | Flags | Value |
|---|---|---|---|
| `OCP_WARNING` | `0x0400` | NOTIFY | mA |
| `OCP_CRITICAL` | `0x0401` | NOTIFY | mA |

Channel = the measured channel.

## POWER_VREG (`0x05xx`)

A regulator with one output. Prefix `VREG_`.

| Op | Wrapper | Args | Answer |
|---|---|---|---|
| `0x0500` SET_ENABLE | `sys_power_vreg_set_enable(dev, on)` | `{u8 enable}` (0 / 1) | – |
| `0x0501` SET_VOLTAGE | `sys_power_vreg_set_voltage(dev, mV)` | `{u32 voltage_mV}` | – |
| `0x0502` SET_CURRENT | `sys_power_vreg_set_current(dev, mA)` | `{u32 current_mA}` | – |

- Voltage and current are the wanted settings. A device with an output of its own keeps them while the output is off and applies them at enable (TPS55289). A device whose output is a USB-PD contract requests them from the charger at once; a request the charger refuses is not kept, and the read answers the last one it took (AP33772S, see POWER_USB_PD).
- Out of the chip's range: `ERR_DEV_RANGE {id, value, min, max}`, never clamped. Writes while suspended: `ERR_DEV_SUSPENDED`.

| Event | ID | Flags | Value |
|---|---|---|---|
| `OVP` | `0x0500` | NOTIFY | mV: the voltage set |
| `OCP` | `0x0501` | NOTIFY | mA: the current limit |
| `SCP` | `0x0502` | NOTIFY | 0 |
| `READY` | `0x0503` | 0 (code flow) | mV: the output settled after SET_ENABLE or a higher SET_VOLTAGE on a running rail, which return at once |

Channel 0.

## POWER_USB_PD (`0x07xx`)

A USB Power Delivery sink. Prefix `PD_` (`PD_OP_*`, `PD_HANDLER`, `PD_ENTRY`).

| Op | Wrapper | Args | Answer |
|---|---|---|---|
| `0x0700` SET_SETTINGS | `sys_power_usb_pd_set_settings(dev, mV, mA)` | `{u32 voltage_mV, u32 current_mA}` | – |
| `0x0701` LIST_OPTIONS | `sys_power_usb_pd_list_options(dev, out, max, &count)` | – | `sys_power_usb_pd_option_t[]` (8 B each: slot, type, min / max mV, max mA), at most 13 |
| `0x0702` GET_LIMITS | `sys_power_usb_pd_get_limits(dev, &mV, &mA)` | – | `{i32 mV, i32 mA}`: the highest-voltage offer |

The charger may not grant exactly what SET_SETTINGS asks; the app reads the result back. Option types: fixed 0, PPS 1, AVS 2.

## POWER_SUPPLY (`0x08xx`)

A power mux: two switched sources into one load (the LM73100 ideal diodes of an H-bridge VM node). Prefix `SUPPLY_` (`SUPPLY_OP_*`, `SUPPLY_HANDLER`, `SUPPLY_ENTRY`).

| Op | Wrapper | Args | Answer |
|---|---|---|---|
| `0x0800` SELECT | `sys_power_supply_select(dev, source)` | `{u8 source}`: 0 off, 1 A, 2 B (`sys_power_supply_source_e`) | – |
| `0x0801` GET_SOURCE | `sys_power_supply_get_source(dev, &source)` | – | u8 source: what is switched on now |

- The source that is on is switched off first, the device's switch delay passes, then the new one is switched on: the two are never on together. A failure leaves both off.
- The chosen source is kept through a suspend: a suspend switches both off, and RESUME switches the chosen one back on.
- A source above 2: `ERR_DEV_RANGE`. SELECT while suspended: `ERR_DEV_SUSPENDED`; GET_SOURCE is allowed. No events.
- The VM has a block for SELECT (`components/vm/vm_blocks/device-contract/power_supply`).

## Devices

| Contract | Implemented by |
|---|---|
| POWER_MONITOR | INA3221 (readings, alerts), AP33772S (readings), DRV8962 (CURRENT of a channel from its IPROPI pins; offered only with them wired, `has_op`) |
| POWER_VREG | TPS55289, AP33772S |
| POWER_USB_PD | AP33772S |
| POWER_SUPPLY | power mux (`components/devices/device_power_mux`) |

The power manager is its own component: SYS_POWER_MANAGER.MD.

## Errors (`ERR_MOD_SYS_POWER` = 10, `sys_power.h`)

Besides the shared `ERR_DEV_*` (RANGE, SUSPENDED): `ERR_POWER_BAD_CHANNEL {id, channel}`, `ERR_POWER_ALERT_UNSUPPORTED {id, channel, alert}`, `ERR_POWER_EXT_FEEDBACK {id}` (a regulator strapped for an external divider: its output can't be set in mV; TPS55289), `ERR_POWER_NO_PROFILE {id, voltage_mV, lowest_mV}` (a USB-PD source offers no profile at or below the voltage; `lowest_mV` is its lowest fixed offer (5 V when it offers none); AP33772S).
