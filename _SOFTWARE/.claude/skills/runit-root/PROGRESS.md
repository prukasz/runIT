# runIT — Progress Log

Last updated: 2026-09-23 · branch `vm_ui` (all firmware work since 2026-09-22 is uncommitted)

Status legend: 🟡 in progress · ⏳ planned · ⛔ blocked. Finished items move to the done log as one line; their design lives in the skill files and component docs.

## runit-esp32 (firmware)

| Area | Status | Notes |
|---|---|---|
| Board test | ⏳ | Nothing since 2026-09-22 has run on hardware: power manager, events, error handling, responses. `CONFIG_RUNIT_SKIP_DEVICE_INIT=y` in `sdkconfig` for the empty devkit — set `n` on the board |
| Power manager | 🟡 | Built (`runit-esp/architecture.md` §4.9, SYS_POWER.MD). To confirm: INA3221 channel order, source indicator pins, reserve / efficiency estimates (`runit_board_defs.h`, `runit_board_cfg.c`). USB-PD uses the offered, not the negotiated current |
| Events (`sys_event`) | 🟡 | Built, not tested on the board (§4.11, SYS_EVENT.MD). Device JSON doesn't list the events each device publishes (no annotation directive yet) |
| Error handling | 🟡 | Open: hbridge error map with `dev_id` in `device_id_of()`; i2c bus-recovery hook (`runit-esp/errors.md` §2) |
| DRV8962 H-bridge device | 🟡 | Driver + adapter + hbridge contract. Missing: install decoder + annotations + JSON, `codecs` REQUIRES, board config (`runit-esp/devices.md` §4) |
| Device adapter fixes | ⏳ | GPIO ESP safe suspend (`devices.md` §5.1); TPS55289 no I2C probe; TCA6424A extra `_new`/`_delete` exports |
| Freeze reconsideration | ⏳ | Write → read-back chains can't complete while frozen; options and coverage gaps in `devices.md` §5.2 |
| Features analysis | ⏳ | On hold (architecture.md §4.4): F-2 type confusion, F-17 lifecycle/fault handling |
| Naming cleanup | ⏳ | `runit-esp/conventions.md` §5.2 |
| `utils.h` static allocator fixes | ⏳ | `runit-esp/conventions.md` §1 (linkage, double start, stack param, buffer capacity, …) + hard-coded stacks → Kconfig |
| Boot-log findings | ⏳ | Flash size 2 MB configured vs 16 MB on board; BLE advertising started twice; CPU 160 MHz (240 possible) (`runit-esp/build.md` §2a) |
| Component docs missing | ⏳ | `sys_i2c`, `sys_data_connector`, `sys_hbridge` have no `.MD` (ask the user for purpose first, conventions.md §8) |
| VM core | 🟡 | Blocks: branch, clone, edge, expr, for, io_set_level, io_toggle, set, timer. No event block reads the VM event buffer yet |
| Features: servo, hbridge | 🟡 | Skeletons (`components/features/`) |
| Headers cleanup | 🟡 | Part 1 done; packet defines moved to Kconfig |
| H-bridge power-source selection (LM73100) | ⏳ | Choose rail A, rail B or direct input per H-bridge group (TCA6424A pins); must prevent shorting two sources. Needs TCA pin mapping |
| ADS7128 digital-input mode | ⏳ | ADC read mapped to a level by threshold (below 0.5 V low, hysteresis TBD); regenerate JSON |
| Wi-Fi coexistence | ⏳ | Not implemented |
| User transports (LoRa, sub-GHz) | ⏳ | Concept only |
| Host tests | ⏳ | No firmware test target; `tools/tests/test_sys_errors.py` referenced by SYS_ERRORS.MD is gone |

## runit-app

| Area | Status | Notes |
|---|---|---|
| BLE backend (Web Bluetooth adapter, GATT catalog) | 🟡 | `app/src/backend/ble/`, test UI `BleTestApp.tsx` |
| Packet packer + data stream / outgoing router | 🟡 | `app/src/backend/packetPack/`, `stream/` |
| Command responses (stream 0x05) | ⏳ | Decode `[0x05][class][packet][status][data]`, match FIFO, OK data via the contract's `response` layout, errors as `u16 tag, u16 owner` (`contracts.generated.json` → `response_stream`, `response`) |
| Power page | ⏳ | Status (`0x3C` response), PSU / battery settings (settings class `0x08`), response matrix (`0x3D`), monitor alerts (`0x39`); enums `sys_power_source_e`, `sys_power_response_e`, `sys_power_battery_e` |
| Event subscriptions page | ⏳ | Class `0x09` (`events` catalog): subscribe returns an ID, unsubscribe by ID / 255 = all; enums `sys_event_domain_e` + per-domain event enums |
| Consume generated JSON descriptors | ⏳ | from `data-structures/*.generated.json` |
| Layout (topbar, sidebar, bottombar, tabs) | ⏳ | Designed in legacy docs `00–14_step_*` (backup: `../_agents_backup_2026-09-22/.agents/skills/runit-app-design/docs/`) |
| Devices view, hardware presence, relations | ⏳ | design docs 07–09 |
| Blocks palette, search, object tree, flow canvas | ⏳ | design docs 10–13 |
| Board pinout SVG, remote controller, telemetry/recorder | ⏳ | design docs 04–06 |
| Capacitor Android build | ⏳ | Web-first, mobile later |

## Repo / tooling

| Item | Status | Notes |
|---|---|---|
| `runit-esp` skill | 🟡 | Pending: vm.md |
| `runit-app` skill | ⏳ | Stub |
| Migrate legacy skill content | ⏳ | From `../_agents_backup_2026-09-22/`; legacy firmware skill has absolute links to another machine (`C:/Users/krolp/...`) |
| JSON annotations → `data-structures/` | 🟡 | Device, contracts, settings, VM model generators + schemas; uncommitted |

## Done log

- 2026-09-22 — Root skill, VISION, HARDWARE, PROGRESS; old agent files stripped (backup `../_agents_backup_2026-09-22/`).
- 2026-09-22 — Architecture review; explicit wiring from runit; `sys_errors` sink injection + error-map folders; `SE_MUST_USE` and all dropped errors fixed.
- 2026-09-22 — Contracts / dispatch / units / dead code (F-13/14/16/18); const vtables + per-instance pin locks; device ID on every device error; no driver/adapter logs.
- 2026-09-22 — Faults → suspend; onboard device protection; owners per module (F-4); settings store (`sys_settings`); command responses (F-12); power manager built.
- 2026-09-22 — Flash + serial capture on COM3 (`runit-esp/scripts/serial_capture.py`).
- 2026-09-23 — Error handling review (severity, async log task, stable tag IDs, renames), F-11 wiring, repeat suppression.
- 2026-09-23 — Events (`sys_event`, F-15) replace callbacks; subscribe / unsubscribe packets (class `0x09`).
- 2026-09-23 — Legacy `Python/` tooling deleted (class resolution moved to `data-structures/auto-annotations/packet_classes.py`); root `auto-annotations/` removed.
