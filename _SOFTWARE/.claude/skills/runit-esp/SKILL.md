---
name: runit-esp
description: Firmware skill for runIT-ESP32 (ESP-IDF v6.1, FreeRTOS, ESP32-S3) — contracts, devices (class, create frame, operations), sys_* modules, error chain, BLE/data connector and VM. Load for any work under runit-esp/ (components/, main/), and for building, configuring (Kconfig) or flashing the firmware.
---

# runIT-ESP32 — Firmware Skill

Session context and hardware: `runit-root` skill ([SKILL.md](../runit-root/SKILL.md), [HARDWARE.md](../runit-root/HARDWARE.md)).

**The component docs are the source of truth.** Every component has a `<NAME>.MD` (its API, wire format, behaviour) and an `AGENTS.md` (the rules for changing it) next to its code. Read both for the component you touch. This skill only holds the map, the rules that span components, and the recipes (build, tests).

**Every change updates the docs it makes stale, in the same change, without being asked:** the component's `*.MD` and `AGENTS.md`, this skill (the component table, architecture.md, conventions.md, errors.md, build.md), `runit-root` (repo map, HARDWARE.md), `templates/AGENTS.md` for wire / descriptor changes, `CLAUDE.md` and `.claude/memory.md`. A new component adds its row here; a removed one removes it. Docs describe the current state only: no history, no dates of changes, no legacy references.

## Skill files — read on demand

| File | Read when |
|---|---|
| [architecture.md](architecture.md) | How the components fit together: layers, boot, the command / stream / error / event flows, where each piece is wired |
| [conventions.md](conventions.md) | Writing or changing any firmware code: static RTOS objects (`utils.h`), constants vs Kconfig, no leftovers, validate once, naming, comments and docs |
| [errors.md](errors.md) | Raising, propagating, reporting errors; severities; adding tags |
| [build.md](build.md) | Building, Kconfig changes (reconfigure / refresh-config), flashing, serial capture, clangd, **test suites** (§6: router, VM, board, BLE) |

## Component docs

Paths are under `runit-esp/components/`. Each `core`, `sys` and `vm` folder also holds its `AGENTS.md`.

| Layer | Component | Doc | Covers |
|---|---|---|---|
| core | `core/utils` | `utils.h` | static RTOS objects (`U_*_DEFINE`), `MUST_USE`, `MSEC`, lists |
| core | `core/errors` | `ERRORS.MD` | the error chain, tags and modules (`err_modules.h`), severity, reporting, wire format, the error record (`err_record`, `err_cmd_t`) |
| core | `core/time` | `TIME.MD` | the board's clock (`board_time_*`): set by a client, UTC once set, the uptime; everything that needs the time takes it from here |
| core | `core/cmd` | `CMD.MD` | the command handler type, the direction (read / write sides, the mirror), `cmd_reply`, `ERR_CMD_*`; what the router and the device registry both build on |
| core | `core/router` | `ROUTER.MD` | the commands of this board (`err_cmd_t` and their answer callbacks), root keys (`router_ids.h`), dispatcher tasks, command tables and op macros, tap, origin, urgent work |
| sys | `sys/sys_data_provider` | `SYS_DATA_PROVIDER.MD` | providers: the ways bytes go out and come in (a BLE characteristic, the UART), by id, one user each; their drivers (key `0x0D`) |
| sys | `sys/sys_session` | `SYS_SESSION.MD` | sessions: a parent, children and clients on framed providers, the frame header, HELLO / PING, ids, the route and forwarding, the board's calls to other boards, answers, stream frames, gone peers and the dead-man's silence (key `0x0E`) |
| sys | `sys/sys_watch` | `SYS_WATCH.MD` | watches: reads this board runs by a period, a change or an event, their entries sent to the one who added them (key `0x0F`) |
| sys | `sys/sys_data_connector` | `SYS_DATA_CONNECTOR.MD` | outbound streams (connectors): bindings to providers, peers subscribed over their session |
| sys | `sys/sys_uart`, `sys/sys_ble` | `SYS_UART.MD`, `SYS_BLE.MD` | the console and the board-to-board link as binary providers (COBS + CRC16); BLE (GATT, MTU, fragment framing, the runIT pair and LOGS providers) |
| sys | `sys/sys_system` | `SYS_SYSTEM.MD` | system commands (key `0x01`): ping, stop, restart, clock; the safe state and the dead-man |
| sys | `sys/sys_log` | `SYS_LOG.MD` | ESP_LOG hook, log options (key `0x08`) |
| sys | `sys/sys_i2c` | `SYS_I2C.MD` | buses, diagnostics (key `0x02`) |
| sys | `sys/sys_event` | `SYS_EVENT.MD` | events per contract, publish, NOTIFY stream, C listeners |
| sys | `sys/sys_device` | `SYS_DEVICE.MD`, `SYS_DEVICE_DRIVER.MD` | device registry, create / remove, ops, sweeps, dependencies, error policy, remote devices (key `0x05`); writing a driver |
| sys | `sys/sys_remote` | `SYS_REMOTE.MD` | devices of other boards: the remote hook of sys_device, calls over the session, reads from the watches' copies, the board's events |
| sys | `sys/sys_io`, `sys/sys_power`, `sys/sys_hbridge` | `SYS_IO.MD`, `SYS_POWER.MD`, `SYS_HBRIDGE.MD` | the contracts as u16 device ops (IO, POWER_MONITOR / VREG / SUPPLY / USB_PD, HBRIDGE) |
| sys | `sys/sys_power_manager` | `SYS_POWER_MANAGER.MD` | budget, sources, battery, response matrix, VREG guard (key `0x0A`) |
| sys | `sys/sys_actions` | `SYS_ACTIONS.MD` | records under u32 IDs with classes, record / upload / readback / run (key `0x06`) |
| sys | `sys/sys_project` | `SYS_PROJECT.MD` | the stored project: sections, chunks, the start section, commit, load / run, what happens at start (key `0x0C`) |
| devices | `devices/device_<chip>` | `device_<chip>.json`, the header | one driver each (ESP32-S3 GPIO, PCA9685, TCA6424A, TPS55289, INA3221, AP33772S, POWER_MUX, DAC53202, ADS7128, SERVO, DRV8962, DC_MOTOR): the descriptor and the header comments are its doc; how to write one is `SYS_DEVICE_DRIVER.MD` (reference driver `device_pca9685`) |
| vm | `vm/vm_core` | `VM_CORE.MD` | the program runtime: model, store, loader, supervisor, telemetry, retained values (key `0x0B`) |
| vm | `vm/vm_blocks` | `VM_BLOCKS.MD`, `<block>/<name>.md` | the block palette, device blocks, adding a block; one user guide per block |
| app | `app/runit` | `RUNIT.MD` | boot steps, safe state, error policy wiring, onboard actions, board config and stream routing |

Descriptors (`device_*.json`, `contract_*.json`, `block_*.json`, `errors.json`, `board.json`) live next to the code and follow `templates/*.schema.json`: [templates/AGENTS.md](../../../templates/AGENTS.md). `sh templates/sync.sh` regenerates and validates them into `runit-app/generated/`.

## Quick build

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 | Out-Null; idf.py -C C:\Users\lukasz\Documents\runIT\_SOFTWARE\runit-esp build
```

After editing any `Kconfig`: run `idf.py reconfigure`. If you changed an option's `default`, run `idf.py refresh-config --policy=kconfig` instead. See [build.md](build.md) §3.
