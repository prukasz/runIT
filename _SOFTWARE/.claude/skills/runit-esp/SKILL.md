---
name: runit-esp
description: Firmware skill for runIT-ESP32 (ESP-IDF v6.1, FreeRTOS, ESP32-S3) — contracts, devices (class, create frame, operations), sys_* modules, error chain, BLE, streams and VM. Load for any work under runit-esp/ (the firmware) or runit-esp-archive/ (the parts not moved yet), and for building, configuring (Kconfig) or flashing the firmware.
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

The firmware is `runit-esp/` (the ESP-IDF project built with its `build.ps1`). Paths below are under `runit-esp/components/`. Each component also holds its `AGENTS.md`.

| Layer | Component | Doc | Covers |
|---|---|---|---|
| core | `core/utils` | `utils.h`, `module_id.h`, `cmd_types.h` | static RTOS objects (`U_*_DEFINE`), `MUST_USE`, `MSEC`, lists; one number per module (`MODULE_ID_DECLARE` in the module's `<module>_id.h`); the command's types every layer names (`cmd_req_t`, `cmd_in_t`, `cmd_out_t`) |
| core | `core/errors` | `ERRORS.MD` | the error chain (an opaque `err_h`: `err_tag`, `err_cause`, `err_data`, `err_find`), tags under the modules' numbers, severity, reporting and the sinks, the error record (`err_record`; a command's `err_record_cmd`, over `cmd_req_t` from `utils`) |
| core | `core/cmd` | `CMD.MD` | the handler type (`cmd_f`, `cmd_reply`, `ERR_CMD_*`), the direction (read / write sides, the mirror), the frame size, the op tables (`cmd_table.h`) |
| core | `core/router` | `ROUTER.MD` | the tree of module routers (`router_table.h`, `ROUTER_MODULE_REGISTER`), root keys = module numbers |
| core | `core/router_queue` | `ROUTER_QUEUE.MD` | the commands of this board in a queue: `cmd_req_t` (a command and its origin), the work and urgent tasks, answers to the callbacks |
| core | `core/timer` | `TIMER.MD` | the board's timers (`board_timer`) |
| core | `core/nvs_store` | `NVS_STORE.MD` | the non-volatile store: the API (`nvs_store.h`) with no type of an implementation, the interface an implementation fills (`nvs_store_iface.h`, `nvs_store_impl`), this chip's in `esp/` (`nvs_flash`); the partitions' start, the one place that erases them; a JSON file as chunks (`0x16`) |
| core | `core/board_id` | `BOARD_ID.MD` | the board's identity: its id in the store (the header's `src`, from the next start once saved), its boot id, its build; this chip's part in `esp/` |
| core | `core/data_matrix` | `DATA_MATRIX.MD` | the port (`data_matrix_port_t`: a module's, a provider's, a session's) and the switch of the ports (`0x30`): a port's output to the inputs of the ports linked from it (`data_matrix_send`, to one `_send_to`, to all but one `_send_except`), unchanged, synchronous; `CONNECT` / `DISCONNECT` / `LIST` |
| sys | `sys/sys_event` | `SYS_EVENT.MD` | events per module, lanes (`SYS_EVENT_LANES`: `SYS`, `DEVICE`), each a ring and a task; publish, C listeners |
| sys | `sys/sys_event_stream` | `SYS_EVENT_STREAM.MD` | the events as two streams of the data matrix: the notify events out (`0x06`), the events of other boards in (`0x07`) |
| sys | `sys/sys_device` and `sys_device_event`, `sys_device_timer`, `sys_device_router`, `sys_device_policy` | `SYS_DEVICE.MD`, `SYS_DEVICE_EVENT.MD`, `SYS_DEVICE_TIMER.MD`, `SYS_DEVICE_ROUTER.MD`, `SYS_DEVICE_POLICY.MD` | the device registry (create / remove, ops, sweeps, dependencies, remote devices; `0x20`), a device that hears events, a device's timers, the registry's commands, the error policy (`0x21`) |
| sys | `sys/sys_io`, `sys/sys_power`, `sys/sys_hbridge`, `sys/bus/sys_i2c`, `sys/bus/sys_uart_contract` | `SYS_IO.MD`, `SYS_POWER.MD`, `SYS_HBRIDGE.MD`, `SYS_I2C.MD`, `SYS_UART_CONTRACT.MD` | the contracts as u16 device ops: IO (`0x01`), I2C (`0x02`: a bus is a device), POWER_MONITOR / VREG / USB_PD / SUPPLY (`0x04`, `0x05`, `0x07`, `0x08`), HBRIDGE (`0x09`), UART (`0x13`: a port is a device made by CREATE) |
| sys | `sys/sys_power_manager` | `SYS_POWER_MANAGER.MD` | budget, sources, rail faults, the VREG guard as an interceptor of the registry (`0x22`) |
| sys | `sys/sys_data_provider` | `SYS_DATA_PROVIDER.MD` | providers: the ways bytes go out and come in, by id, each a port of the data matrix; their lifecycle, one call into a driver at a time, their events (`0x0D`) |
| sys | `sys/sys_session` | `SYS_SESSION.MD` | a session is a framed provider with one peer: the hello, the dead-man, broadcast, calls with a wait, PUSH frames (`0x0E`) |
| sys | `sys/sys_watch` | `SYS_WATCH.MD` | watches: reads this board runs by a period or a change, sent to the one who added them; the client's cache (`0x2B`) |
| sys | `sys/sys_remote` | `SYS_REMOTE.MD` | devices of other boards: the registry's remote hook, calls over the session, reads from the watches, their events and errors (`0x2F`) |
| sys | `sys/bus/sys_ble` | `SYS_BLE.MD` | the BLE peripheral: NimBLE, the run-time GATT table, the framing both ways (`0x2E`) |
| sys | `sys/sys_system` | `SYS_SYSTEM.MD` | system commands (`0x28`): ping, stop, restart, clock; the safe state as the registry's sweep, the dead-man |
| sys | `sys/sys_log` | `SYS_LOG.MD` | ESP_LOG hook, the error record history (`0x2A`) |
| sys | `sys/sys_actions` | `SYS_ACTIONS.MD` | stored command lists: records under u32 IDs with classes, upload / readback / RUN / RUN_ALL (`0x2C`) |
| sys | `sys/sys_board` | `SYS_BOARD.MD` | the board's profile: the record run at boot, the JSON the app reads back (`0x2D`) |
| sys | `sys/sys_lvgl` | `SYS_LVGL.MD` | the LVGL adapter (`0x29`); its DISPLAY and TOUCH contracts are not in the rework yet, so it does not build |
| devices | `devices/{soc,chips,virtual}/device_<chip>` | `device_<chip>.json`, the header | one driver each: ESP32-S3 GPIO, I2C and UART (soc), PCA9685, TCA6424A, TPS55289 (chips), DC_MOTOR (virtual): the descriptor and the header comments are its doc |
| providers | `providers/provider_{uart,console,ble}` | `PROVIDER_UART.MD`, `PROVIDER_CONSOLE.MD`, `PROVIDER_BLE.MD` | the provider drivers (a struct with the base first, `in`, `remove`, the RX flags in their cfg): a UART device, the console (UART0 + USB-Serial-JTAG), BLE characteristics |
| app | `app/runit` | `RUNIT.MD` | the composition: `runit_start` (the boot order), the data matrix's tables, the error sink, the board's profile |

**Not in the rework yet**, still in `runit-esp-archive/components/` (the old firmware: it does not build any more, its parts are the reference for moving them): `core/time` (`TIME.MD`, the board's clock: the rework builds it from there), `sys/sys_display`, `sys/sys_touch`, `sys/sys_lvgl` (the old adapter, which `lvgl-playground` builds), `sys/sys_project`, `sys/sys_ota`, `sys/sys_flasher` (with its host tests), `vm/vm_core`, `vm/vm_blocks` (`VM_CORE.MD`, `VM_BLOCKS.MD`), the devices `device_ina3221`, `device_ap33772s`, `device_power_mux`, `device_dac53202`, `device_ads7128`, `device_servo`, `device_drv8962`, the old `app/runit`, `main/` and `boards/`. The host test runner stays in `runit-esp-archive/test_host/`.

Descriptors (`device_*.json`, `contract_*.json`, `block_*.json`, `errors.json`, `board.json`) live next to the code and follow `templates/*.schema.json`: [templates/AGENTS.md](../../../templates/AGENTS.md). `sh templates/sync.sh` regenerates and validates them into `runit-app/generated/`.

## Quick build

```powershell
cd C:\Users\Lukasz\Documents\runIT\_SOFTWARE\runit-esp; .\build.ps1   # .\build.ps1 reconfigure after a Kconfig or a new folder
```

After editing any `Kconfig`: run `idf.py reconfigure`. If you changed an option's `default`, run `idf.py refresh-config --policy=kconfig` instead. See [build.md](build.md) §3.
