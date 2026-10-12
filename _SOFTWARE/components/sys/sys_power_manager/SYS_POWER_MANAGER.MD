# sys_power_manager — power budget, source, battery, responses

Component `components/sys/sys_power_manager/` (2026-10-11). Port of the legacy power manager (`legacy/components/system/sys_power/sys_power_manager.c`, SYS_POWER.MD there) on the new tree, with three changes: the manager can be **turned off** (measurement only), its limits can be **strict or soft** (a margin over the budget), and a **source pin** tells battery from USB. The contracts it uses (POWER_MONITOR, POWER_VREG) are `sys_power`'s (SYS_POWER.MD).

| File | Contents |
|---|---|
| `sys_power_manager.h` | Enums, manager events (`PWRM_EV_*`), board description, settings / status structs, `ERR_SYS_POWER_MANAGER_MAP` (`ERR_MOD_SYS_POWER_MANAGER` = 14), API |
| `sys_power_manager.c` | Task, budget, the POWER_VREG guard, source, battery, response matrix, settings. `WHOLE_ARCHIVE` (tag registration) |
| `sys_power_manager_router.c/.h` | Remote, `ROUTER_KEY_POWER` = 0x0A |
| `Kconfig` | Task, period (1 s), rails (4), budget step (250 mW), input warning (90 %), battery hysteresis (3 %) |

## Board description (`sys_power_board_t`)

Static, from the board config (`runit_board_cfg.c`, values from the legacy rev 1): `max_mV` / `max_mA` 20 V / 5.5 A (input trip, budget caps), `reserve_mW` 3667 (the 3.3 V buck), `unknown_source_mA` 1000, `vreg_efficiency_pct` 90, `input` = INA3221 (id 12) channel 1, `consumers` = TPS55289 rail A (id 10, monitor ch 2) and rail B (id 11, ch 0), `min_mA` 200 (`TPS55289_MIN_CURRENT_MA`), `responses` = the board's default responses (below); a consumer may carry its own `responses` (NULL = the board's). The devices don't have to exist: a missing input monitor is skipped until it answers once, a missing rail counts as off (its state is forgotten, so a new one starts from scratch).

## Settings (records, SYS_ACTIONS.MD)

SET applies a setting at once and stores nothing; the manager starts from the defaults below after a reset. The app keeps each setting as the record that holds its SET frame, `[0x0A][op][args]`, saved with upload or recording; whoever starts the project runs the records before it enables any rail, so the limits and the battery protection are in force first. Nothing replays by itself.

| Record ID | Frame | Contents |
|---|---|---|
| `0x01000002` | SET_CONFIG `sys_power_manager_cfg_t`, 8 B | `enabled` (1), `strict` (1), `soft_margin_pct` (20, 0..100), `source_pin` (`sys_io_pin_t`, `SYS_IO_PIN_NONE`), `battery_level` (1), `pd_max_auto` (1, 0 / 1: on a USB source always negotiate the charger's highest offer, below) |
| `0x01000003` | SET_PSU `sys_power_psu_t`, 8 B | `max_mV`, `max_mA` of an external supply; 0 / 0 removes it |
| `0x01000004` | SET_BATTERY `sys_power_battery_t`, 4 B | `chemistry` (NONE / LIPO), `cells` (1 .. max_mV / 4200), `low_pct` (2..100), `critical_pct` (1..low − 1) |
| `0x01000005` | SET_RESPONSES `u8[1 + rails][11]` | the whole response matrix (below): row 0 the board, row 1 + i rail i, by kind. The single-cell SET_RESPONSE changes one cell; save the matrix with SET_RESPONSES (another size, the board's rails changed, is `ERR_CMD_BAD_LEN`) |

Out of range: `ERR_PWRM_BAD_ARG {value, min, max}`. A change is applied at once: the task is woken and recomputes the budget.

### Modes

| `enabled` | `strict` | Behaviour |
|---|---|---|
| 1 | 1 | **Strict** (legacy): the rails are held to the **budget** |
| 1 | 0 | **Soft**: held to **budget × (100 + soft_margin_pct) / 100**; the input's hardware alerts move up with it |
| 0 | – | **Measurement only**: input, source, battery and the status keep working, the manager's own events are still published; no budget check, automatic rails get `max_mA`, no input alerts programmed, every response is NOTIFY |

The guard keeps tracking the rails while disabled, so turning it back on starts from their real state.

### Source

- **`source_pin` set**: made an input on its device (if it is already configured, read as it is), read every tick: `battery_level` → **BATTERY**, the other level → **USB**. Unreadable (device not created yet): UNKNOWN, retried.
- **The board's eFuse pins** (`source_pins` in the board description, 2026-10-11; runIT: VUSB_OK = TCA 3, VEXT_OK = TCA 4, read as plain inputs, high = live): the external input wins when live (the board's mux gives it priority) → **PSU**, or **BATTERY** if a battery is configured and no supply entered; else USB live → **USB**; neither (or the pins unreadable: the expander not created yet) → the next rule. Checked after `source_pin`, which stays a user override.
- **No pin** (legacy inference without USB-PD): a PSU entered → PSU, else a battery configured → BATTERY, else UNKNOWN.

Source current: PSU → its `max_mA` (not entered: the board's `max_mA`, 5.5 A: the eFuse limits an external supply; enter the real limit with `SET_PSU`); BATTERY → `max_mA` (the board limits, not the battery); USB → the highest offer of the board's USB-PD sink (`usb_pd` in the board description: `GET_LIMITS`), `unknown_source_mA` while it is not created or offers nothing; UNKNOWN → `unknown_source_mA`. A change publishes `SOURCE_CHANGED`.

## Budget and rails

Every tick (`CONFIG_SYS_POWER_MANAGER_PERIOD_MS`, at once after a setting): **budget** = measured input mV × source mA, both capped by the board; **limit** = the budget (strict) or with the margin (soft). The input monitor's critical alert is set at the limit's current, the warning at 90 % of it. When the budget moves by ≥ 250 mW (or a setting changed) the automatic rails are rebalanced; if the reserve and the fixed rails no longer fit, or an automatic rail's share is 0: `ERR_PWRM_BELOW_ALLOCATION {allocated_mW, limit_mW}` and `BUDGET_EXCEEDED` (latched until it fits again).

**The guard.** The manager is the POWER_VREG guard (`sys_device_set_guard`, SYS_DEVICE.MD "Guards"): every SET_ENABLE / SET_VOLTAGE / SET_CURRENT of a budgeted rail goes through it, **remote commands included** (legacy only saw calls made through `sys_power_vreg_*`). Rules as in legacy:
- a rail's input power = mV × mA × 100 / efficiency;
- **fixed rail** (SET_CURRENT > 0): must fit `limit − reserve − other fixed rails`, else `ERR_PWRM_BUDGET_EXCEEDED {id, requested_mW, available_mW}` (wrapped in `ERR_DEV_ID{id}`, so the device's error policy sees it);
- **automatic rail** (SET_CURRENT 0, the default): the rails without a user limit split what's left equally, each capped at `max_mA`; a share below the regulator's `min_mA` refuses the change (`ERR_PWRM_BUDGET_EXCEEDED`, requested = the rail at `min_mA`) and rolls it back; an automatic rail can't be turned on when nothing is left;
- order: when a rail grows, the other automatic rails are lowered first; a hardware failure rolls the rail's state back;
- SET_VOLTAGE > `max_mV` / SET_CURRENT > `max_mA`: `ERR_DEV_RANGE`; the device checks its own range after that;
- a rail that isn't created passes (`ERR_DEV_NOT_FOUND` from the registry); a suspended or missing rail is skipped by the rebalance.

## Battery

LiPo resting-voltage curve per cell (4.20 V = 100 %, 3.84 V = 50 %, 3.27 V = 0 %, 21 points), only while the source is BATTERY. `BATTERY_LOW` / `BATTERY_CRITICAL` once at or below the threshold, re-armed 3 % above it.

## Events and the response matrix

The manager's own events (contract `0x06`, **source `SYS_DEVICE_ID_NONE`** = 0xFFFF, channel 0, all NOTIFY):

| Event | ID | Value | Kind |
|---|---|---|---|
| `SOURCE_CHANGED` | `0x0600` | new source | SOURCE_CHANGED |
| `BATTERY_LOW` | `0x0601` | % | BATTERY_LOW |
| `BATTERY_CRITICAL` | `0x0602` | % | BATTERY_CRITICAL |
| `BUDGET_EXCEEDED` | `0x0603` | limit mW | BUDGET_EXCEEDED |
| `INPUT_OVP` | `0x0604` | input mV | OVP |

It also listens (queued) to every VREG and monitor event: `VREG OVP` → OVP, `VREG OCP` → OCP_CRITICAL, `VREG SCP` → SPC, monitor `OCP_WARNING` / `OCP_CRITICAL` → the same. A rail's own events and its monitor channel's alerts get **that rail's row** of the matrix and act on that rail; the input channel's alerts and the manager's events get the **board row** and act on every rail. A fault is also reported as `ERR_PWRM_FAULT {source, channel, kind, response, value}` (MEDIUM), so it reaches the errors stream with the response taken.

| Response | Rail | Board |
|---|---|---|
| `0` NOTIFY | event only | event only |
| `1` DISABLE | SUSPEND that rail | SUSPEND every rail |
| `2` RESET | RESET that rail | RESET every rail |
| `3` SAFE_STATE | the app's safe state (`runit_enter_safe_state`) | same |

Kinds (legacy numbers, the matrix index): 1 OVP, 2 UVP, 3 SPC, 4 OCP warning, 5 OCP critical, 6 OTP, 7 battery low, 8 battery critical, 9 budget exceeded, 10 source changed. UVP and OTP have no source yet. A rail's row only matters for OVP, SPC and the two OCP kinds (the other kinds are board events). Defaults (rev 1, board and both rails): OVP, SPC, budget exceeded → DISABLE; OTP, battery critical → SAFE_STATE; the rest NOTIFY (OCP critical too: a TPS55289 OCP means the rail is in its current limit, which the chip holds by itself).

**Per target (2026-10-11).** One row for the board and one per rail, so rail A (a motor) can keep running in its limit while rail B (logic) is cut. `SET_RESPONSE {target, kind, response}`: target `0xFFFF` (`SYS_POWER_TARGET_BOARD`) or the rail's device ID; another ID: `ERR_PWRM_NOT_RAIL {id}`. Stored (`pwr_resp`, the whole matrix) before it is applied: a failed store changes nothing. How the chip itself protects (OCP delay, hiccup, which faults are reported) is the device's own setting (DEVICE_TPS55289.MD "Protection"); the user's own logic on an event is the VM's.

## Remote (`ROUTER_KEY_POWER` = 0x0A)

| Op | Request | Response |
|---|---|---|
| `0x01` SET_CONFIG | `sys_power_manager_cfg_t` (7 B) | – |
| `0x02` GET_CONFIG | – | 7 B |
| `0x03` SET_PSU | `{u32 max_mV, u32 max_mA}` | – |
| `0x04` SET_BATTERY | `{u8 chemistry, u8 cells, u8 low_pct, u8 critical_pct}` | – |
| `0x05` SET_RESPONSE | `{u16 target, u8 kind, u8 response}` | – (stored) |
| `0x06` GET_RESPONSES | `{u16 target}` | `u8[11]` by kind (index 0 unused) |
| `0x08` NEGOTIATE_MAX | – | – (once, by hand; `ERR_PWRM_NO_OFFER` when nothing is offered) |
| `0x07` GET_STATUS | – | `sys_power_status_t` (31 B): source, input mV / mA, source mV / mA, budget, limit, allocated (mW), battery % (0xFF unknown), flags (1 enabled, 2 strict, 4 over budget, 8 input seen) |

**Negotiating the highest offer (2026-10-11).** `NEGOTIATE_MAX` (by hand) and the `pd_max_auto` setting (by itself, every tick while the source is USB) ask the board's USB-PD sink (`usb_pd`, the AP33772S) for the charger's highest-voltage offer (`GET_LIMITS`) with the current offered at it, both capped by the board's `max_mV` / `max_mA` (20 V / 5.5 A), through `SET_SETTINGS`. The automatic one asks again only when the offer differs from its last request (a new charger, a changed setting, a source change) and reports a refusal once. A plain 5 V USB offers nothing: `NEGOTIATE_MAX` answers `ERR_PWRM_NO_OFFER`, the automatic one does nothing. The budget follows the measured input voltage and the offered current on its own. Not run against a PD charger yet.

`0x09` SET_RESPONSES `u8[1 + rails][11]` (no header; the whole matrix, each response `NOTIFY..SAFE_STATE`; another size `ERR_CMD_BAD_LEN`, nothing changes). Records are read back with the actions READ (IDs `0x01000002`..`05`).

## Changes from legacy

- New: `enabled` / `strict` / `soft_margin_pct` (replace `pwr_caps`: "caps disabled" ≈ `enabled` 0), `source_pin` + `battery_level` (replace the board's `source_pins` list), `limit_mW` and `flags` in the status, the VREG guard (remote commands budgeted too), forgotten state of removed rails.
- Not ported: the power mux contract. USB-PD: the AP33772S is ported (DEVICE_AP33772S.MD); legacy took USB-PD as a source by itself when the chip offered power, here the source pin (or the PSU / battery inference) says USB and the chip only gives the current.
- u16 device IDs; events per contract instead of one power event enum (the enum stays as the matrix's "kind"); errors in their own module (`ERR_PWRM_*`).
- The response matrix has a row per rail and is stored (legacy: one runtime row).
- Legacy issues addressed: the manager no longer reports the input monitor missing at boot (skipped until seen, as legacy did); a budget refusal is `ERR_PWRM_BUDGET_EXCEEDED`, not a driver range error (legacy fixed it with `min_mA`, kept); soft mode gives a small source (USB 5 V / 1 A) room above the computed budget.

## Tested

Builds. Not run on the PCB (needs the INA3221 and the TPS55289 rails). On the devkit: `router_tests.py` `test_power` (config, settings, source pin, status, responses, a rail that isn't created); the per-target responses (2026-10-11) are written into it, not run.
