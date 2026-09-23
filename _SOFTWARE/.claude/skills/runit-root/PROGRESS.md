# runIT — Progress Log

Last updated: 2026-09-23 · branch `vm_ui` (work through 2026-09-23 committed in `574ba86`)

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
| Component docs missing | ⏳ | `sys_i2c`, `sys_hbridge` have no `.MD` (ask the user for purpose first, conventions.md §8) |
| Data connector rework | 🟡 | Built, not run on hardware (SYS_DATA_CONNECTOR.MD). To check on the board: responses reach the app, device-initiated MTU exchange raises the frame limit (logs no longer cut to 20 B), 249–512 B writes accepted (bigger BLE buffers), class `0x06` protection, `seq` echoed in responses. Open: Wi-Fi / UART / MQTT providers |
| App: data connector IDs | ⏳ | Connector / provider IDs are published in `enums.json` (`sys_data_connector_id_e`, `runit_data_provider_e`) and referenced by the class `0x06` fields (`enum_ref`); the app doesn't consume `enums.json` yet |
| App: VM program compiler | ⏳ | Build programs from `vm-blocks.generated.json` (block ids, pins, state layouts) + `vm-model.generated.json` (object header, accessors) + `enums.json`; create a VM-routed event subscription (class `0x09`) for programs with ON_EVENT blocks |
| VM core | 🟡 | 15 blocks on one palette table (`g_vm_block_types`), catalog `data-structures/vm/vm-blocks.generated.json`. Open (`runit-esp/vm/findings.md`): `0x04` record layouts in JSON (G-2), test target (G-5, needs a toolchain decision), program storage on the device (G-8, next), NVS / flash size (G-9). Latch/periodic/action/on_event not run on hardware |
| Features: servo, hbridge | 🟡 | Skeletons (`components/features/`) |
| Headers cleanup | 🟡 | Part 1 done; packet defines moved to Kconfig |
| H-bridge power-source selection (LM73100) | ⏳ | Choose rail A, rail B or direct input per H-bridge group (TCA6424A pins); must prevent shorting two sources. Needs TCA pin mapping |
| ADS7128 digital-input mode | ⏳ | ADC read mapped to a level by threshold (below 0.5 V low, hysteresis TBD); regenerate JSON |
| Wi-Fi coexistence | ⏳ | Not implemented |
| User transports (LoRa, sub-GHz) | ⏳ | Concept only |
| Host tests | ⛔ | Wanted for the VM core (`runit-esp/vm/findings.md` G-5). Blocked: no host C compiler on this machine; decide between installing one (LLVM-MinGW / Zig) or an on-board Unity test app |

## runit-app

| Area | Status | Notes |
|---|---|---|
| BLE backend (Web Bluetooth adapter, GATT catalog) | 🟡 | `app/src/backend/ble/`, test UI `BleTestApp.tsx` |
| Packet packer + data stream / outgoing router | 🟡 | `app/src/backend/packetPack/`, `stream/` |
| Command responses (stream 0x05) | ⏳ | Every command must start with a `seq` byte: `[seq][class][packet][payload]` (`packPacket` doesn't add it yet; a raw-hex test UI must too). Decode `[0x05][seq][class][packet][status][data]`, match pending commands by `seq` with a timeout, OK data via the contract's `response` layout, errors as `u16 tag, u16 owner` (`contracts.generated.json` → `response_stream`, `response`) |
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
| `runit-app` skill | ⏳ | Stub |
| Migrate legacy skill content | ⏳ | From `../_agents_backup_2026-09-22/`; legacy firmware skill has absolute links to another machine (`C:/Users/krolp/...`) |
| JSON annotations → `data-structures/` | 🟡 | Device, contracts, settings, VM model generators + schemas |

## Done log

- 2026-09-22 — Root skill, VISION, HARDWARE, PROGRESS; old agent files stripped (backup `../_agents_backup_2026-09-22/`).
- 2026-09-22 — Architecture review; explicit wiring from runit; `sys_errors` sink injection + error-map folders; `SE_MUST_USE` and all dropped errors fixed.
- 2026-09-22 — Contracts / dispatch / units / dead code (F-13/14/16/18); const vtables + per-instance pin locks; device ID on every device error; no driver/adapter logs.
- 2026-09-22 — Faults → suspend; onboard device protection; owners per module (F-4); settings store (`sys_settings`); command responses (F-12); power manager built.
- 2026-09-22 — Flash + serial capture on COM3 (`runit-esp/scripts/serial_capture.py`).
- 2026-09-23 — Error handling review (severity, async log task, stable tag IDs, renames), F-11 wiring, repeat suppression.
- 2026-09-23 — Events (`sys_event`, F-15) replace callbacks; subscribe / unsubscribe packets (class `0x09`).
- 2026-09-23 — VM retained values: `retentive` objects kept by name in one `sys_settings` record, saved every 60 s (changed only) off the VM task, on teardown and safe state; restored on the first start; `0x48 0A` forgets them; `sys_settings_load_blob()`.
- 2026-09-23 — VM structure: one palette entry per block type (`vm_block_type_t`, shape checked once at load, no per-pass re-check); per-call fault bit replaces the global `g_vm_block_fault`; `ON_EVENT` block; block catalog generator `generate-vm-blocks.py` (+ schema), block enums and `vm_exec_command_e` published.
- 2026-09-23 — VM patches: B-1 fixed (a block rejected by its verify is removed again, `vm_store_undo`); LATCH, PERIODIC, ACTION blocks; `sys_actions_request()` queue so the VM never runs an action in a pass; `@verified` markers removed; VM.MD / VM_EXEC.MD brought up to date (status table), perf history moved to VM_PERF.MD, stale self-test/benchmark references removed.
- 2026-09-23 — `runit-esp` skill complete: VM subfolder `vm/` (README, blocks, wire, findings) from a review of `components/VM`.
- 2026-09-23 — Command sequence byte: `[seq][class][packet]…`, echoed in responses; contracts JSON `matching: seq`.
- 2026-09-23 — Data connector made transport-agnostic: ID-based API, `uint32` endpoints, provider-owned framing, responses to the frame's origin, dynamic frame limit (BLE MTU) instead of truncation, protected system connectors, BLE provider moved into `ble`, `SYS_DATA_CONNECTOR.MD`.
- 2026-09-23 — Legacy `Python/` tooling deleted (class resolution moved to `data-structures/auto-annotations/packet_classes.py`); root `auto-annotations/` removed.
