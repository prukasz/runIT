# sys_power_manager — power budget, source, battery, responses

Component `components/sys/sys_power_manager/`. It watches one board input, the power source behind it, and the regulator rails fed from it. It keeps the rails within the source's budget, asks the USB-PD charger for the voltage set, estimates the battery charge, and answers power faults with a response matrix. What the board has (rails, monitors, source pins, the sink) is fixed in the board config; what the user chooses is set remotely. The contracts it uses (POWER_MONITOR, POWER_VREG) are `sys_power`'s (SYS_POWER.MD).

| File | Contents |
|---|---|
| `sys_power_manager.h` | Enums, manager events (`PWRM_EV_*`), board description, settings and status structs, `ERR_SYS_POWER_MANAGER_MAP` (`ERR_MOD_SYS_POWER_MANAGER` = 14), API |
| `sys_power_manager.c` | Task, budget, the POWER_VREG guard, source, battery, response matrix, settings. `WHOLE_ARCHIVE` (error tag registration) |
| `sys_power_manager_router.c/.h` | Remote interface, `ROUTER_KEY_POWER` = 0x0A |
| `contract_power_manager.json` | The router's descriptor (`templates/`) |
| `Kconfig` | Task, period (1 s), rails (4), budget step (250 mW), input warning (90 %), battery hysteresis (3 %) |
| `AGENTS.md` | Rules for changing this folder |

## Board description (`sys_power_board_t`)

Static, from the board config (`components/app/runit/runit_board_cfg.c`, constants in `runit_board_defs.h`). Each power element is tied to its measurement here: a rail to its regulator and monitor channel, the board input to its monitor channel. The rails' own limits (voltage, current) are set by the user on the regulator itself (POWER_VREG frames, through the guard below).

| Field | Value | Meaning |
|---|---|---|
| `max_mV` / `max_mA` | 20 V / 5.5 A | Hardware trip of the input; the caps of the budget |
| `reserve_mW` | 3667 | Always taken by fixed loads (the 3.3 V buck) |
| `unknown_source_mA` | 1000 | Current assumed while the source's limit is unknown |
| `vreg_efficiency_pct` | 90 | Rail input power = output power × 100 / efficiency |
| `input` | INA3221 (id 12), channel 1 | Measures the board input |
| `consumers` | TPS55289 rail A (id 10, monitor channel 2), rail B (id 11, monitor channel 0) | The budgeted rails |
| `min_mA` | 200 (`TPS55289_MIN_CURRENT_MA`) | The lowest limit a rail can be programmed to |
| `usb_pd` | AP33772S (id 13) | The USB-PD sink |
| `source_pins` | TCA6424A pins 3 (VUSB_OK) and 4 (VEXT_OK); devkit none | The eFuse power-good outputs; none: SET_CONFIG says the source |
| `responses` | the board's defaults (below) | A consumer may carry its own; NULL = the board's |

The board's devices are created at boot and can't be removed (`ERR_DEV_SYSTEM`, SYS_DEVICE.MD), so a rail never disappears. The manager starts before them: a monitor is skipped until it answers once, a rail counts as off until it exists, and one whose creation failed stays off.

## Settings (records, SYS_ACTIONS.MD)

SET applies a setting at once and stores nothing. After a reset the manager starts from the defaults below. The app keeps each setting as a record that holds its SET frame, `[0x0A][op][args]`, saved with the upload or the recording. Nothing replays by itself: the project runs the records when it starts.

| Record ID | Frame | Contents |
|---|---|---|
| `0x01000002` | SET_CONFIG `sys_power_manager_cfg_t`, 4 B | `enabled` (0 / 1), `strict` (0 / 1), `soft_margin_pct` (0..100), `source` (`sys_power_source_e`: the source on a board without source pins, ignored with them) |
| `0x01000003` | SET_PSU `sys_power_psu_t`, 8 B | `max_mV`, `max_mA` of an external supply; 0 / 0 removes it |
| `0x01000004` | SET_BATTERY `sys_power_battery_t`, 4 B | `chemistry` (NONE / LIPO), `cells` (1 .. max_mV / 4200), `low_pct` (2..100), `critical_pct` (1..low − 1) |
| `0x01000005` | SET_RESPONSES, `u8[1 + rails][11]` | The whole response matrix (below) |
| `0x01000008` | SET_PD `sys_power_pd_t`, 5 B | `mode` (`sys_power_pd_mode_e`: OFF, MAX, VOLTAGE), `voltage_mV` (VOLTAGE: 1 .. `max_mV`) |

Out of range: `ERR_PWRM_BAD_ARG {value, min, max}`. A change applies at once: the task is woken and recomputes the budget.

Defaults: enabled, strict, soft margin 20 %, source UNKNOWN, PD MAX, no PSU, no battery.

### Modes

| `enabled` | `strict` | Behaviour |
|---|---|---|
| 1 | 1 | **Strict**: the rails are held to the **budget** |
| 1 | 0 | **Soft**: held to **budget × (100 + soft_margin_pct) / 100**. The input's hardware alerts move up with it |
| 0 | – | **Measurement only**: input, source, battery and the status keep working, and the manager's own events are still published. No budget check. Automatic rails get `max_mA`. No input alerts are programmed. Every response is NOTIFY |

The guard keeps tracking the rails while the manager is disabled, so turning it back on starts from their real state.

### Source

The source is one of UNKNOWN, USB, PSU or BATTERY. Its current limit caps the budget.

- **The eFuse pins** (`source_pins`): the external input wins when it is live. The eFuse only says the input is live, so a configured battery (SET_BATTERY) makes it BATTERY, else PSU. Otherwise USB-C live → USB. Neither live, or the pins cannot be read (the expander not created yet) → UNKNOWN. SET_CONFIG's `source` is ignored.
- **No source pins** (the devkit): the source is SET_CONFIG's `source` (default UNKNOWN).

Source current: PSU → its entered `max_mA`, or the board's `max_mA` when none is entered (the board's eFuse limits an external supply; enter the real limit with SET_PSU). BATTERY → the board's `max_mA`. USB → the current of the USB-PD profile in use ("USB-PD request"), or `unknown_source_mA` while the sink is not created or offers nothing. UNKNOWN → `unknown_source_mA`. A change publishes `SOURCE_CHANGED`.

## Budget and rails

Every tick (`CONFIG_SYS_POWER_MANAGER_PERIOD_MS`, and at once after a setting):

- **Budget** = measured input mV × source mA, both capped by the board.
- **Limit** = the budget (strict), or the budget with the margin (soft).
- The input monitor's critical alert is set at the limit's current, and its warning at `CONFIG_SYS_POWER_MANAGER_INPUT_WARNING_PCT` (90 %) of it.
- When the budget moves by at least `CONFIG_SYS_POWER_MANAGER_BUDGET_STEP_MW` (250 mW), or a setting changed, the automatic rails are rebalanced.
- If the reserve and the fixed rails no longer fit, or an automatic rail's share is 0, the manager reports `ERR_PWRM_BELOW_ALLOCATION {allocated_mW, limit_mW}` and publishes `BUDGET_EXCEEDED`. The state stays latched until it fits again.

**The guard.** The manager is the POWER_VREG guard (`sys_device_set_guard`, SYS_DEVICE.MD "Guards"). Every SET_ENABLE, SET_VOLTAGE and SET_CURRENT of a budgeted rail goes through it, remote commands included.

- A rail's input power = mV × mA × 100 / efficiency.
- **Fixed rail** (SET_CURRENT > 0): must fit `limit − reserve − the other fixed rails`. Otherwise `ERR_PWRM_BUDGET_EXCEEDED {id, requested_mW, available_mW}`, wrapped in `ERR_DEV_ID` and `ERR_DEV_OP` by the registry, so the device's error policy sees it.
- **Automatic rail** (SET_CURRENT 0, the default): the rails without a user limit split what is left, equally, each capped at `max_mA`. A share below the regulator's `min_mA` refuses the change (`ERR_PWRM_BUDGET_EXCEEDED`, requested = the rail at `min_mA`) and rolls it back. An automatic rail cannot be turned on when nothing is left.
- Order: when a rail grows, the other automatic rails are lowered first. A hardware failure rolls the rail's state back.
- SET_VOLTAGE above `max_mV` or SET_CURRENT above `max_mA`: `ERR_DEV_RANGE`, never clamped. The device checks its own range after that.
- A rail that is not created passes the guard: the registry answers `ERR_DEV_NOT_FOUND`. A suspended or missing rail is skipped by the rebalance.

## Battery

LiPo per-cell curve (4.20 V = 100 %, 3.84 V = 50 %, 3.27 V = 0 %; 21 points, linear between them). The estimate uses the measured input voltage, and only while the source is BATTERY. `BATTERY_LOW` and `BATTERY_CRITICAL` are published once at or below their threshold, and re-armed `CONFIG_SYS_POWER_MANAGER_BATTERY_HYSTERESIS_PCT` (3 %) above it.

## Events and the response matrix

The manager's own events (contract `0x06`, **source `SYS_DEVICE_ID_NONE`** = 0xFFFF, channel 0, all NOTIFY):

| Event | ID | Value | Kind |
|---|---|---|---|
| `SOURCE_CHANGED` | `0x0600` | the new source | SOURCE_CHANGED |
| `BATTERY_LOW` | `0x0601` | % | BATTERY_LOW |
| `BATTERY_CRITICAL` | `0x0602` | % | BATTERY_CRITICAL |
| `BUDGET_EXCEEDED` | `0x0603` | the limit, mW | BUDGET_EXCEEDED |
| `INPUT_OVP` | `0x0604` | input mV | OVP |
| `PD_FIXED` | `0x0605` | the voltage in use, mV | – (information: no response) |

The manager also listens (queued) to the rails' and monitors' events and maps them to a kind:

| Device event | Kind |
|---|---|
| VREG `OVP` | OVP |
| VREG `OCP` | OCP_CRITICAL |
| VREG `SCP` | SPC |
| monitor `OCP_WARNING` / `OCP_CRITICAL` | OCP_WARNING / OCP_CRITICAL |

A rail's own events, and the alerts of its monitor channel, get **that rail's row** of the matrix and act on that rail. The input channel's alerts and the manager's own events get the **board row** and act on every rail. Each fault is also reported as `ERR_PWRM_FAULT {source, channel, kind, response, value}` (MEDIUM), so it reaches the errors stream with the response taken.

| Response | Rail | Board |
|---|---|---|
| `0` NOTIFY | event only | event only |
| `1` DISABLE | SUSPEND that rail | SUSPEND every rail |
| `2` RESET | RESET that rail | RESET every rail |
| `3` SAFE_STATE | the app's safe state (`runit_enter_safe_state`) | the same |

Kinds (the matrix index, also on the wire): 1 OVP, 2 UVP, 3 SPC, 4 OCP warning, 5 OCP critical, 6 OTP, 7 battery low, 8 battery critical, 9 budget exceeded, 10 source changed. UVP and OTP have no source yet. Only OVP, SPC and the two OCP kinds reach a rail's row. The other kinds are board events.

Defaults (the board config, both rails the same): OVP, SPC and budget exceeded → DISABLE; OTP and battery critical → SAFE_STATE; the rest NOTIFY. OCP critical stays NOTIFY: a TPS55289 at its OCP limit holds its current by itself.

**Per target.** One row for the board and one per rail, so rail A can keep running in its limit while rail B is cut. The matrix is set whole (`SET_RESPONSES`, rows in GET_MATRIX's order) and read with its targets (`GET_MATRIX`: `0xFFFF` = `SYS_POWER_TARGET_BOARD`, else the rail's device ID). The change applies at once. The app saves it as the record `0x01000005`. How the chip protects itself (OCP delay, hiccup, which faults it reports) is the device's own setting.

## USB-PD request

SET_PD says what to ask the charger for. Every tick on a USB source the manager reads the sink's offers (`LIST_OPTIONS`), picks one and, when it differs from the last request, sends it (`SET_SETTINGS`). A new charger is asked again.

| `mode` | Voltage asked |
|---|---|
| `0` OFF | nothing is asked: the charger's 5 V |
| `1` MAX (default) | the highest voltage offered |
| `2` VOLTAGE | `voltage_mV` |

- The voltage is capped by the board's `max_mV`. A range profile (PPS, then AVS) that holds it gives the voltage itself; otherwise the highest fixed profile at or below it is taken, the same choice the sink makes.
- The current is the most the chosen profile offers, capped by `max_mA`. It is also the USB source's current for the budget.
- VOLTAGE answered by a lower fixed profile publishes `PD_FIXED` (value: the voltage in use), once per request.
- GET_STATUS shows the profile in use: `pd_mV`, `pd_mA`, `pd_type` (0 fixed, 1 PPS, 2 AVS, 0xFF none).
- A refused request is reported once (the sink's error), not every tick.

## Remote (`ROUTER_KEY_POWER` = 0x0A)

| Op | Request | Response |
|---|---|---|
| `0x01` SET_CONFIG | `sys_power_manager_cfg_t` (4 B) | – |
| `0x02` GET_CONFIG | – | `sys_power_manager_cfg_t` (4 B) |
| `0x03` SET_PSU | `{u32 max_mV, u32 max_mA}` | – |
| `0x04` SET_BATTERY | `{u8 chemistry, u8 cells, u8 low_pct, u8 critical_pct}` | – |
| `0x07` GET_STATUS | – | `sys_power_status_t` (40 B): `u8 source, i32 input_mV, i32 input_mA, u32 source_mV, u32 source_mA, u32 budget_mW, u32 limit_mW, u32 allocated_mW, u8 battery_pct` (0xFF = unknown), `u8 flags` (1 enabled, 2 strict, 4 over budget, 8 input seen), `u32 pd_mV, u32 pd_mA, u8 pd_type` |
| `0x08` SET_PD | `sys_power_pd_t` (5 B): `u8 mode, u32 voltage_mV` | – |
| `0x09` SET_RESPONSES | `u8[1 + rails][11]`, no header: the whole matrix | – (another size: `ERR_CMD_BAD_LEN`; nothing changes) |
| `0x0A` GET_PSU | – | `sys_power_psu_t` (8 B), as set |
| `0x0B` GET_BATTERY | – | `sys_power_battery_t` (4 B), as set |
| `0x0C` GET_MATRIX | – | `sys_power_response_row_t[1 + rails]`: per row `u16 target` (0xFFFF the board, else the rail's device ID), `u8[11]` by kind; the rows in SET_RESPONSES's order |
| `0x0D` GET_PD | – | `sys_power_pd_t` (5 B), as set |

Every SET has its read (`read_by` in the descriptor: GET_CONFIG, GET_PSU, GET_BATTERY, GET_MATRIX, GET_PD), so the app reads the settings when it connects and shows the board's values.

## Errors (`ERR_MOD_SYS_POWER_MANAGER` = 14)

| Error | Severity | Fields | Meaning |
|---|---|---|---|
| `ERR_PWRM_NOT_READY` | HIGH | – | Called before `sys_power_manager_init` |
| `ERR_PWRM_BUDGET_EXCEEDED` | MEDIUM | `id, requested_mW, available_mW` | A rail change that does not fit |
| `ERR_PWRM_BELOW_ALLOCATION` | MEDIUM | `allocated_mW, limit_mW` | The budget shrank under what is committed |
| `ERR_PWRM_FAULT` | MEDIUM | `source, channel, kind, response, value` | A hardware fault and the response taken |
| `ERR_PWRM_BAD_ARG` | HIGH | `value, min, max` | A setting out of range |

Besides these, the device errors it passes on: `ERR_DEV_RANGE`, `ERR_DEV_SUSPENDED`, `ERR_DEV_NOT_FOUND`.

## Limits

- The USB-PD negotiation has not been run against a PD charger yet.
- The module has not been run on the PCB, which needs the INA3221 and the TPS55289 rails.
- The hardware test is `router_tests.py` `test_power` (config, settings, source, status, responses). It is not run on the board yet.
