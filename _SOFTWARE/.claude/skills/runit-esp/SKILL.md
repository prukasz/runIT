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
| core | `core/crc` | `CRC.MD` | the board's CRCs: `crc32_ieee` (the ROM's on this chip), every CRC-32 of the firmware |
| core | `core/nvs_store` | `NVS_STORE.MD` | the non-volatile store: the API (`nvs_store.h`) with no type of an implementation, the interface an implementation fills (`nvs_store_iface.h`, `nvs_store_impl`), this chip's in `esp/` (`nvs_flash`); the partitions' start, the one place that erases them; a JSON file as chunks (`0x16`) |
| core | `core/board_id` | `BOARD_ID.MD` | the board's identity: its id in the store (the header's `src`, from the next start once saved), its boot id, its build; this chip's part in `esp/` |
| core | `core/data_matrix` | `DATA_MATRIX.MD` | the port (`data_matrix_port_t`: a module's, a provider's, a session's) and the switch of the ports (`MODULE_ID_DATA_MATRIX`): a port's output to the inputs of the ports linked from it (`data_matrix_send`, to one `_send_to`, to all but one `_send_except`), unchanged, synchronous; `CONNECT` / `DISCONNECT` / `LIST` |
| sys | `sys/sys_event` | `SYS_EVENT.MD` | events per module, lanes (`SYS_EVENT_LANES`: `SYS`, `DEVICE`), each a ring and a task; publish, C listeners |
| sys | `sys/sys_event_stream` | `SYS_EVENT_STREAM.MD` | the events as one port of the data matrix, `EVENTS`: the notify events out, other boards' in (published without NOTIFY, passed on) |
| sys | `sys/sys_device` and `sys_device_event_contract`, `sys_device_timer`, `sys_device_router`, `sys_device_policy` | `SYS_DEVICE.MD`, `SYS_DEVICE_EVENT_CONTRACT.MD`, `SYS_DEVICE_TIMER.MD`, `SYS_DEVICE_ROUTER.MD`, `SYS_DEVICE_POLICY.MD` | the device registry (create / remove, ops, sweeps, dependencies, remote devices), a device that hears events, a device's timers, the registry's commands, the error policy |
| sys | `sys/sys_io_contract`, `sys/sys_power_contract`, `sys/sys_hbridge_contract`, `sys/sys_display_contract`, `sys/bus/sys_i2c_contract`, `sys/bus/sys_spi_contract`, `sys/bus/sys_uart_contract` | `SYS_IO_CONTRACT.MD`, `SYS_POWER_CONTRACT.MD`, `SYS_HBRIDGE_CONTRACT.MD`, `SYS_I2C_CONTRACT.MD`, `SYS_UART_CONTRACT.MD` | the contracts as u16 device ops: IO, I2C (a bus is a device), SPI (a bus is a device), DISPLAY, POWER_MONITOR / VREG / USB_PD / SUPPLY, HBRIDGE, UART (a port is a device made by CREATE) |
| sys | `sys/sys_power_manager` | `SYS_POWER_MANAGER.MD` | budget, sources, rail faults, the VREG guard as an interceptor of the registry |
| sys | `sys/sys_data_provider` | `SYS_DATA_PROVIDER.MD` | providers: the ways bytes go out and come in, by id, each a port of the data matrix; their lifecycle, one call into a driver at a time, their events |
| sys | `sys/sys_session` | `SYS_SESSION.MD` | a session is a framed provider with one peer: the hello, the dead-man, broadcast, calls with a wait, PUSH frames |
| sys | `sys/sys_watch` | `SYS_WATCH.MD` | watches: reads this board runs by a period or a change, sent out of the port `WATCH` to the session of the one who added them; the client's cache |
| sys | `sys/sys_device_remote` | `SYS_DEVICE_REMOTE.MD` | devices of other boards: the registry's remote hook, calls through the router to a board's session, reads from the watches, their events and errors through the port `REMOTE` |
| sys | `sys/bus/sys_ble` | `SYS_BLE.MD` | the BLE peripheral: NimBLE, the run-time GATT table, the framing both ways |
| sys | `sys/sys_system` | `SYS_SYSTEM.MD` | system commands: ping, stop, restart, clock, the tasks' free stack; the safe state as the registry's sweep, the dead-man |
| sys | `sys/sys_log` | `SYS_LOG.MD` | ESP_LOG hook, the error record history; the ports `LOGS` and `ERRORS` (this board's records out, other boards' in and passed on) |
| sys | `sys/sys_actions` | `SYS_ACTIONS.MD` | stored command lists: records under u32 IDs with classes, upload / readback / RUN / RUN_ALL |
| sys | `sys/sys_board` | `SYS_BOARD.MD` | the board's profile: the record run at boot, the JSON the app reads back |
| sys | `sys/sys_render` | `sys_render.h`, `router/sys_render_router.h` | the render adapter on LVGL 9.3 (`idf_component.yml`, `lvgl_impl/lv_conf.h` via `LV_CONF_PATH` + `LV_KCONFIG_IGNORE`, router under `MODULE_ID_RENDER`) |
| devices | `devices/{soc,chips,virtual}/device_<chip>` | `device_<chip>.json`, the source's comments | one driver each, one source: ESP32-S3 GPIO, I2C, UART and SPI (`device_esp32_spi`) (soc); PCA9685, TCA6424A, TPS55289, INA3221, AP33772S, DAC53202, ADS7128, DRV8962, POWER_MUX, SPI_LCD (`device_spi_lcd`: ILI9341 and ST7789 behind the display contract) (chips); DC_MOTOR, SERVO (virtual): the descriptor and the comments at the top of the source are its doc |
| providers | `providers/provider_{uart,console,ble}` | `PROVIDER_UART.MD`, `PROVIDER_CONSOLE.MD`, `PROVIDER_BLE.MD` | the provider drivers (a struct with the base first, `in`, `remove`, the RX flags in their cfg): a UART device, the console (UART0 + USB-Serial-JTAG), BLE characteristics |
| vm | `vm/vm_core`, `vm/vm_blocks` | `VM_CORE.MD`, `VM_BLOCKS.MD` | the VM, moved from the old firmware and built with the project; only partly ported: the errors (one map for the core and the blocks in `vm/errors/`, the core tags for the generic cases), the router's number (`MODULE_ID_VM`) and the events follow the rework, the telemetry goes out of the port `VM_STREAM` of the data matrix (`vm_sub.c`, linked to the app's sessions in `runit`); the device-contract blocks (`vm_blocks/device-contract/`) and the device-custom blocks are not built (they call the old device API); the router's table, the retained values on `nvs_store` and a data input are open (VM_CORE.MD "Open") |
| app | `app/runit` | `RUNIT.MD` | the composition: `runit_start` (the boot order), the data matrix's tables, the error sink, the board's profile |

**Not in the rework yet**, still in `runit-esp-archive/components/` (the old firmware: it does not build any more, its parts are the reference for moving them): `core/time` (`TIME.MD`, the board's clock: the rework builds it from there), `sys/sys_touch`, `sys/sys_lvgl` (the old adapter, which `lvgl-playground` builds), `sys/sys_project`, `sys/sys_ota`, `sys/sys_flasher`, the old `app/runit`, `main/` and `boards/`. The host tests' runner is `runit-esp/test_host/` (build.md §6).

Descriptors (`device_*.json`, `contract_*.json`, `block_*.json`, `errors.json`, `board.json`) live next to the code and follow `templates/*.schema.json`: [templates/AGENTS.md](../../../templates/AGENTS.md). `sh templates/sync.sh` regenerates and validates them into `runit-app/generated/`.

## Quick build

```powershell
cd C:\Users\Lukasz\Documents\runIT\_SOFTWARE\runit-esp; .\build.ps1   # .\build.ps1 reconfigure after a Kconfig or a new folder
```

After editing any `Kconfig`: run `idf.py reconfigure`. If you changed an option's `default`, run `idf.py refresh-config --policy=kconfig` instead. See [build.md](build.md) §3.
