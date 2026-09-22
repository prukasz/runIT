# System Power Manager (sys_power)

IMPORTANT: If you modify the source files in this component (sys_power.h, sys_power.c or sys_power_manager.c), you must update this documentation to reflect the changes.

## Overview

`sys_power` has two parts:

- **Power contracts:** dispatch for voltage regulators (VREG), power monitors and USB-PD sinks (`sys_power.c`).
- **Power manager:** the board has one power input, and the manager keeps everything fed from it within that input's budget (`sys_power_manager.c`):
  - it identifies the source and derives the budget from it;
  - it caps the vreg rails;
  - it estimates battery charge;
  - it answers power events through a response matrix.

Nothing in the manager assumes a board revision: the board config describes the layout in a `sys_power_board_t`.

```
components/system/sys_power/
├── CMakeLists.txt
├── Kconfig               # manager task, consumer count, thresholds
├── SYS_POWER.MD          # This documentation
├── sys_power.c           # contract dispatch (monitor, USB-PD)
├── sys_power_manager.c   # source, budget, rails, battery, events, settings
├── errors/sys_error_power.h
└── include/sys_power.h
```

---

## Power manager

### Board description (`sys_power_board_t`)

Filled by the board config (`runit_board_cfg.c`) and passed to `sys_power_init()`; it must stay valid (static).

| Field | Meaning (runIT rev 1) |
|---|---|
| `max_mV`, `max_mA` | Hardware trip limits of the input (20 V, 5.5 A). They cap the budget; an input above `max_mV` raises OVP. |
| `reserve_mW` | Input power always taken by fixed loads: the 3.3 V buck for the ESP32 (3.3 V × 1 A at ~90 % ≈ 3667 mW). |
| `unknown_source_mA` | Current assumed while the source's limit is unknown (1 A). |
| `vreg_efficiency_pct` | Rail input power = output power × 100 / efficiency (90 %). |
| `input` | Monitor channel measuring the board input (INA3221 input channel). |
| `usb_pd_id` | USB-PD sink device (AP33772S), or `SYS_POWER_NO_DEVICE`. |
| `source_pins` | Pins identifying the active source (`{pin, active_level, source}`; first match wins). None on rev 1 yet. |
| `consumers` | Budgeted rails: `{vreg_id, monitor}`. Rev 1: both TPS55289 rails with their INA3221 channels. |
| `responses` | Default response per event (the response matrix). |

### Source and budget

The manager task runs every `CONFIG_SYS_POWER_TASK_PERIOD_MS` (1 s), and immediately after a settings change.

1. **Source** (`sys_power_source_e`):
   - If the board has indicator pins, the first pin reading its active level decides; no match means `UNKNOWN`.
   - Otherwise it's inferred: USB-C if the USB-PD sink offers power, else PSU if the user entered one, else battery if one is configured, else unknown.
2. **Input** voltage and current are measured on `input`. A measurement failure is reported once, until it works again.
3. **Budget** = measured input voltage × source current, both capped by `max_mV` / `max_mA`:

   | Source | Current used |
   |---|---|
   | USB-C | The source's offered current (`sys_power_usb_pd_get_limits`) |
   | PSU | The current entered by the user (`sys_power_set_psu`, persisted) |
   | Battery | `max_mA` (a battery isn't current-limited, the board is) |
   | Unknown, or limit not known | `unknown_source_mA` |

4. **Hardware trip on the input:** the input monitor's critical alert is set at the source current, and its warning at `CONFIG_SYS_POWER_INPUT_WARNING_PCT` of it.
5. **Rebalance:** when the budget changes by at least `CONFIG_SYS_POWER_BUDGET_STEP_MW`, the automatic rail caps are recomputed.
6. **Over budget:** if the reserve and the fixed rails no longer fit (or an automatic rail's share is 0), the manager reports `ERR_POWER_SOURCE_BELOW_ALLOCATION` and raises `BUDGET_EXCEEDED` (latched until it fits again).

### Rails

Only rails listed in `consumers` are budgeted; other vregs are dispatched directly. For a budgeted rail, `sys_power_vreg_set_enable` / `set_voltage` / `set_current` go through the manager:

- **Allocation:** a rail's input power is `set_mV × current_mA × 100 / efficiency`.
- **Fixed rail** (current limit set by the user, `set_current(mA > 0)`): its allocation must fit in `budget − reserve − other fixed rails`, otherwise the call fails with `ERR_POWER_BUDGET_EXCEEDED {dev_id, requested_mW, available_mW}`.
- **Automatic rail** (`set_current(0)`, the default): the rails without a user limit split what's left equally. Each gets `share / set_mV` as its current limit, capped by `max_mA`, and the caps are reprogrammed when anything changes. An automatic rail can't be turned on when nothing is left.
- **Ordering:** the hardware is written so the budget isn't exceeded in between. When a rail grows (voltage up, enabled, limit up), the other automatic rails are lowered first. On a hardware error the rail's state is rolled back.
- **What's counted:** only settings made through `sys_power`, so a rail's power-on state isn't counted until it's set.

### Battery

`sys_power_set_battery(chemistry, cells, low_pct, critical_pct)` (persisted):

- **Estimate:** while the source is the battery, the charge is estimated from the measured input voltage per cell using a LiPo resting-voltage curve (4.20 V = 100 %, 3.84 V = 50 %, 3.27 V = 0 %, linear between 21 points).
- **Events:** `BATTERY_LOW` / `BATTERY_CRITICAL` are raised once at or below their threshold. They re-arm when the charge rises `CONFIG_SYS_POWER_BATTERY_HYSTERESIS_PCT` above it.
- **Limits:** `cells` is at most `max_mV / 4200`, and `0 < critical < low <= 100`.

### Events and the response matrix

Every power event (`sys_power_events_e`) has a response (`sys_power_response_e`):

| Response | Rail event | Board-level event |
|---|---|---|
| `NOTIFY` | event only | event only |
| `DISABLE` | suspend that rail | suspend every rail |
| `RESET` | reset that rail | reset every rail |
| `SAFE_STATE` | registered safe state (runit: VM stop + suspend all) | same |

**Where events come from:**
- **Rails** (TPS55289 OVP / OCP critical / SPC) and the rails' monitor channels. Rail faults are always armed.
- **The input monitor channel** (OCP warning / critical): treated as board-level.
- **The manager itself:** OVP on the input, battery low / critical, budget exceeded, source changed. The event value is the input mV, charge %, budget mW or new source.

**Delivery and configuration:**
- Power events are `SYS_EVENT_DOMAIN_POWER` events ([[SYS_EVENT.MD]]). `sys_power_init()` subscribes the manager (queued, system-owned) to every power event; it answers the rails, their monitors and the input, and ignores the rest. A user subscription is a separate listener, so it can't cut the manager off.
- The input alerts are armed with `sys_power_monitor_set_alert(input, OCP_CRITICAL at the source current / OCP_WARNING at CONFIG_SYS_POWER_INPUT_WARNING_PCT)`, reprogrammed when the source current changes.
- The defaults come from the board config. `sys_power_set_response(event, response)` changes one at runtime (not persisted).
- The manager applies the response, then publishes its board-level events with `device_id = SYS_POWER_NO_DEVICE` (255); users get them with an event subscription (device = any).

**rev 1 defaults:**

| Event | Response |
|---|---|
| OVP, SPC, OCP critical, budget exceeded | DISABLE |
| OTP, battery critical | SAFE_STATE |
| UVP, OCP warning, battery low, source changed | NOTIFY |

### Status

`sys_power_get_status()` returns `sys_power_status_t`: source, measured input mV/mA, the source limits used, budget, allocated power (reserve + rails) and battery % (`0xFF` unknown). Packet `0x3C` returns it in the command response.

### Settings

PSU limits (`pwr_psu`) and the battery setup (`pwr_battery`) are stored with `sys_settings` (NVS) and loaded by `sys_power_init()`. Packets: settings class `0x08` (`dec_settings_power.h`): `0x01` set_psu, `0x02` set_battery.

---

## Events (`sys_power_events_e`)

| Value | Event |
|---|---|
| 1 | OVP (over-voltage) |
| 2 | UVP (under-voltage) |
| 3 | SPC (short circuit) |
| 4 | OCP warning |
| 5 | OCP critical |
| 6 | OTP (over-temperature) |
| 7 | Battery low |
| 8 | Battery critical |
| 9 | Budget exceeded |
| 10 | Source changed |

`SYS_PWR_EVENT_COUNT` (11) sizes the response matrix. Device adapters publish events with `sys_power_publish(device_id, channel, event, value, hops)`.

---

## Feature Contract Interfaces (Vtables)

### Voltage Regulator Contract (sys_power_vreg_contract_t)
```c
typedef struct sys_power_vreg_contract_t {
  err_h (*set_enable)(void* device_handle, bool state);
  err_h (*set_voltage)(void* device_handle, uint32_t voltage_mV);
  err_h (*set_current)(void* device_handle, uint32_t current_mA);
} sys_power_vreg_contract_t;
```

### Power Monitor Contract (sys_power_monitor_contract_t)
```c
typedef struct sys_power_monitor_contract_t {
  err_h (*get_voltage)(void* device_handle, uint8_t channel, int32_t* out_mV);
  err_h (*get_current)(void* device_handle, uint8_t channel, int32_t* out_mA);
  /* Hardware current alert: alert = SYS_PWR_EVENT_OCP_CRITICAL or _OCP_WARNING. */
  err_h (*set_alert)(void* device_handle, uint8_t channel, sys_power_events_e alert, int32_t threshold_mA);
} sys_power_monitor_contract_t;
```
A tripped alert is published as that event on that channel.

### USB Power Delivery Contract (sys_power_usb_pd_contract_t)
```c
typedef struct sys_power_usb_pd_contract_t {
  err_h (*set_settings)(void* device_handle, uint32_t voltage_mV, uint32_t current_mA);
  err_h (*list_options)(void* device_handle, sys_power_usb_pd_option_t* out_options, uint8_t max_options, uint8_t* out_count);
  err_h (*get_limits)(void* device_handle, int32_t* out_mV, int32_t* out_mA);
} sys_power_usb_pd_contract_t;
```
`list_options` fills up to `max_options` entries of `sys_power_usb_pd_option_t {slot, type, min_mV, max_mV, max_mA}` with the source's offers (`type`: `sys_power_usb_pd_option_type_e` fixed / PPS / AVS; at most `SYS_POWER_USB_PD_OPTIONS_MAX` = 13). The `0x37` packet returns them in its response.

---

## API

| Function | Purpose |
|---|---|
| `sys_power_init(board)` | Start the manager and subscribe it to power events (boot step, after `sys_settings_init` and `sys_event_init`) |
| `sys_power_register_safe_state(fn)` | Safe state used by `SAFE_STATE` |
| `sys_power_set_psu(mV, mA)` / `sys_power_set_battery(...)` | Persisted source settings |
| `sys_power_set_response(event, response)` | Runtime response matrix |
| `sys_power_get_status(out)` | Manager snapshot |
| `sys_power_get_max_mV()` / `sys_power_get_max_mA()` | Board hardware caps |
| `sys_power_vreg_set_enable/voltage/current` | Budgeted for listed rails (current 0 = automatic) |
| `sys_power_monitor_get_voltage/current`, `sys_power_monitor_set_alert`, `sys_power_usb_pd_*` | Contract dispatch |

## Errors

- `ERR_POWER_BUDGET_EXCEEDED {dev_id, requested_mW, available_mW}`: a rail request that doesn't fit (device-attributed).
- `ERR_POWER_SOURCE_BELOW_ALLOCATION {allocated_mW, budget_mW}`: the budget shrank below what's committed (no device).
- Owners: `OWNER_SYS_POWER_*` (`0xA4xx`), including `MANAGER`, `SOURCE`, `BUDGET`, `EVENTS` and `SETTINGS` for the manager.
