# sys_power — the power contracts

Component `components/sys/sys_power/`. Not a module with state: four device contracts (`POWER_MONITOR` `0x04`, `POWER_VREG` `0x05`, `POWER_USB_PD` `0x07`, `POWER_SUPPLY` `0x08`) whose ops mean the same across every chip implementing them, like the IO contract ([`SYS_IO.MD`](../sys_io/SYS_IO.MD)). Drivers implement the ops they support (`ERR_DEV_UNKNOWN_OP` otherwise). Callers invoke ops directly via `SYS_DEVICE_SET` / `SYS_DEVICE_GET` (`sys_device.h`); only `sys_power_usb_pd_list_options()` and inline `sys_power_mW()` live here.

| File | Contents |
|---|---|
| `sys_power.h` | `sys_power_mW()` (`int64_t` product / 1000) and includes the error map |
| `errors/sys_power_errors.h`, `errors/errors_sys_power.json` | `ERR_SYS_POWER_MAP` (`MODULE_ID_SYS_POWER` = `0x26`) and user messages |
| `contracts/sys_power_monitor.h` | `CONTRACT_PMON` (`0x04`): `CONTRACT_PMON_OPERATIONS`, `PMON_EVENTS`, `sys_power_alert_e` (`//#ref-enum`) |
| `contracts/sys_power_vreg.h` | `CONTRACT_VREG` (`0x05`): `CONTRACT_VREG_OPERATIONS`, `VREG_EVENTS` |
| `contracts/sys_power_usb_pd.h` | `CONTRACT_PD` (`0x07`): `CONTRACT_PD_OPERATIONS`, option/limits structs, `sys_power_usb_pd_list_options()` |
| `contracts/sys_power_supply.h` | `CONTRACT_SUPPLY` (`0x08`): `CONTRACT_SUPPLY_OPERATIONS`, `sys_power_supply_source_e` (`//#ref-enum`) |
| `contracts/contract_power_*.json` | The descriptors of the four contracts (`templates/`) |
| `sys_power.c` | Registers the errors of `MODULE_ID_SYS_POWER` (`ERR_MODULE_DEFINE`) and implements `sys_power_usb_pd_list_options()`. `WHOLE_ARCHIVE`: devices raise `ERR_POWER_*` without calling functions here |
| `CMakeLists.txt` | Requires `errors`, `cmd`, `sys_device`, `sys_event`; linked `WHOLE_ARCHIVE` |
| `AGENTS.md` | Rules for changing this folder |

Routed frames for all four contracts use [`sys_device_router`](../sys_device_router/SYS_DEVICE_ROUTER.MD): `[0x20][dir u8][id u8][op u16][args]` with packed little-endian payloads.

## POWER_MONITOR (`0x04xx`)

Prefix `CONTRACT_PMON` (`CONTRACT_PMON_OP_*`, `OP_DECLARE_READ` / `OP_DECLARE_WRITE`, `OP_ENTRY` / `OP_ENTRY_R` / `OP_ENTRY_W`, `PMON_EV_*`).

```c
int32_t mA = 0;
ERR_TRY(SYS_DEVICE_GET(mon, CONTRACT_PMON, OP_CURRENT, &mA, .channel = 1));
ERR_TRY(SYS_DEVICE_SET(mon, CONTRACT_PMON, OP_ALERT, .channel = 1, .alert = SYS_POWER_ALERT_OCP_WARNING, .threshold_mA = 1500));
```

| Op | Side | Key / write args | Read answer |
|---|---|---|---|
| `0x0400` VOLTAGE | R | `{u8 channel}` (`pmon_channel_args_t`) | `i32` mV |
| `0x0401` CURRENT | R | `{u8 channel}` (`pmon_channel_args_t`) | `i32` mA |
| `0x0402` ALERT | W | `{u8 channel, u8 alert, i32 threshold_mA}` (`pmon_alert_args_t`) | – |
| `0x0403` POWER | R | `{u8 channel}` (`pmon_channel_args_t`) | `i32` mW |

- Readings are signed (a reversed shunt reports a negative current) and allowed while suspended.
- **POWER**: chips with a power register read it directly; others compute `sys_power_mW(mV, mA)` (`int64_t` product so 20 V × 5.5 A does not overflow).
- **Alerts** (`sys_power_alert_e`): `SYS_POWER_ALERT_OCP_WARNING` (`0`), `SYS_POWER_ALERT_OCP_CRITICAL` (`1`). An unsupported alert on a channel returns `ERR_POWER_ALERT_UNSUPPORTED`.

| Event (`MODULE_ID_PMON` = `0x04`) | Code (tag) | Flags | Payload |
|---|---|---|---|
| `PMON_EV_OCP_WARNING` | `0x01` (`0x00040001`) | `SYS_EVENT_NOTIFY` | `i32 mA` |
| `PMON_EV_OCP_CRITICAL` | `0x02` (`0x00040002`) | `SYS_EVENT_NOTIFY` | `i32 mA` |

Channel = measured channel.

## POWER_VREG (`0x05xx`)

A regulator with one output. Prefix `CONTRACT_VREG`.

```c
ERR_TRY(SYS_DEVICE_SET(vreg, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 9000));
ERR_TRY(SYS_DEVICE_SET(vreg, CONTRACT_VREG, OP_ENABLE, .enable = 1));
```

| Op | Side | Args |
|---|---|---|
| `0x0500` ENABLE | W | `{u8 enable}` (`vreg_enable_args_t`: `0` / `1`) |
| `0x0501` VOLTAGE | W | `{u32 voltage_mV}` (`vreg_voltage_args_t`) |
| `0x0502` CURRENT | W | `{u32 current_mA}` (`vreg_current_args_t`) |

- Voltage and current are the requested limits (`device_tps55289` retains them while off and applies them on enable; USB-PD sinks negotiate with the charger).
- Out of chip range: `ERR_DEV_RANGE {id, value, min, max}`, never clamped. Writes while suspended: `ERR_DEV_SUSPENDED`.

| Event (`MODULE_ID_VREG` = `0x05`) | Code (tag) | Flags | Payload |
|---|---|---|---|
| `VREG_EV_OVP` | `0x01` (`0x00050001`) | `SYS_EVENT_NOTIFY` | `i32 mV` (voltage set) |
| `VREG_EV_OCP` | `0x02` (`0x00050002`) | `SYS_EVENT_NOTIFY` | `i32 mA` (current limit) |
| `VREG_EV_SCP` | `0x03` (`0x00050003`) | `SYS_EVENT_NOTIFY` | – |
| `VREG_EV_READY` | `0x04` (`0x00050004`) | `0` (code flow) | `i32 mV` (settled output after `ENABLE` or voltage increase) |

Channel `0`.

## POWER_USB_PD (`0x07xx`)

A USB Power Delivery sink. Prefix `CONTRACT_PD` (`CONTRACT_PD_OP_*`).

| Op | Side | C access | Write args / read answer |
|---|---|---|---|
| `0x0700` SETTINGS | RW | `SYS_DEVICE_SET` / `SYS_DEVICE_GET` (`OP_SETTINGS`) | `{u32 voltage_mV, u32 current_mA}` (`pd_settings_args_t`) |
| `0x0701` OPTIONS | R | `sys_power_usb_pd_list_options(dev, out, max, &count)` | `sys_power_usb_pd_option_t[]` (8 B each: `slot`, `type`, `min_mV`, `max_mV`, `max_mA`), up to `SYS_POWER_USB_PD_OPTIONS_MAX` (`13`) |
| `0x0702` LIMITS | R | `SYS_DEVICE_GET` (`OP_LIMITS`) | `{i32 voltage_mV, i32 current_mA}` (`sys_power_usb_pd_limits_t`): highest-voltage offer |

Option types (`sys_power_usb_pd_option_type_e`): `FIXED` (`0`), `PPS` (`1`), `AVS` (`2`). `OPTIONS` may answer a variable number of items, so `sys_power_usb_pd_list_options()` reads with `sys_device_call()`, validates whole `sys_power_usb_pd_option_t` items (`ERR_CMD_BAD_LEN`), and fills `out_count`.

## POWER_SUPPLY (`0x08xx`)

A two-input power mux switched into one load. Prefix `CONTRACT_SUPPLY` (`CONTRACT_SUPPLY_OP_*`).

| Op | Side | C access | Write args / read answer |
|---|---|---|---|
| `0x0800` SOURCE | RW | `SYS_DEVICE_SET` / `SYS_DEVICE_GET` (`OP_SOURCE`) | `{u8 source}` (`supply_select_args_t`): `0` OFF, `1` A, `2` B (`sys_power_supply_source_e`); read returns what is active |

- Break-before-make switching: turns the active source off before waiting its switch delay and closing the next; failures leave both off.
- Chosen source is preserved across `SUSPEND` (both off) and restored on `RESUME`.
- Source above `2`: `ERR_DEV_RANGE`. Write while suspended: `ERR_DEV_SUSPENDED`; reads are allowed. No events.

## Devices

| Contract | Implemented by |
|---|---|
| `POWER_VREG` (`0x05`) | [`device_tps55289`](../../devices/chips/device_tps55289/device_tps55289.c) |

The power manager is its own component: [`SYS_POWER_MANAGER.MD`](../sys_power_manager/SYS_POWER_MANAGER.MD).

## Errors (`MODULE_ID_SYS_POWER` = `0x26`, `MODULE_ID_SYS_POWER`, `errors/sys_power_errors.h`)

Besides shared `ERR_DEV_*` (`RANGE`, `SUSPENDED`, `BAD_PIN`):
- `ERR_POWER_BAD_CHANNEL {id, channel}`
- `ERR_POWER_ALERT_UNSUPPORTED {id, channel, alert}`
- `ERR_POWER_EXT_FEEDBACK {id}` — regulator strapped for an external voltage divider (`TPS55289`)
- `ERR_POWER_NO_PROFILE {id, voltage_mV, lowest_mV}` — USB-PD sink has no profile at or below requested voltage (`lowest_mV` is lowest fixed offer, or 5 V if none)
