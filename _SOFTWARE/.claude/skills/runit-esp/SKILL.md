---
name: runit-esp
description: Firmware skill for runIT-ESP32 (ESP-IDF v6.1, FreeRTOS, ESP32-S3) — contracts, devices (class, create frame, operations), sys_* modules, error chain, BLE/data connector and VM. Load for any work under components/ or main/, and for building, configuring (Kconfig) or flashing the firmware.
---

# runIT-ESP32 — Firmware Skill

Session context, hardware and progress: `runit-root` skill ([SKILL.md](../runit-root/SKILL.md), [HARDWARE.md](../runit-root/HARDWARE.md), [PROGRESS.md](../runit-root/PROGRESS.md)).

Component `*.MD` files are the source of truth for each module's API. This skill only holds routing, rules that apply across modules, and recipes.

## Files — read on demand

| File | Read when |
|---|---|
| [build.md](build.md) | Building, Kconfig changes (reconfigure / refresh-config), flashing, serial capture, clangd, **hardware test scripts** (§6: UART command link, devkit / VM / persistence / BLE suites) |
| [errors.md](errors.md) | Raising, propagating or handling errors; owners; severity |
| [architecture.md](architecture.md) | Layers and allowed dependencies, boot sequence, command/error/event flows, agreed design, open findings |
| [conventions.md](conventions.md) | Writing or changing any firmware code: static RTOS objects (`utils.h`), constants vs Kconfig, no legacy leftovers, validate-once, naming, `//@` annotations, comments/docs |
| [devices.md](devices.md) | Legacy device shape (class, `g_<chip>_class`): rules and per-device notes still useful for porting. **New tree:** `components/sys/sys_device/SYS_DEVICE.MD` "Writing a device" (type ID in `sys_device_types.h`, `SYS_DEVICE_TYPE_DEFINE`, `WHOLE_ARCHIVE`, no router) |
| [vm/README.md](vm/README.md) | Any VM work: model, code map, runit wiring; routes to [vm/blocks.md](vm/blocks.md) (palette, adding a block), [vm/wire.md](vm/wire.md) (class `0x04`, telemetry, app JSON), [vm/findings.md](vm/findings.md) (stale docs, bugs, gaps) |

## Module docs — read the one you touch

| Doc | Covers |
|---|---|
| `components/system/sys_device/SYS_DEVICE.MD` | Device registry, class, create router and operations, lifecycle ops, dispatch macros, per-instance error handling |
| `components/system/sys_device/DEVICE_CLASS.MD` | Why the class carries the type id, cfg size and pin refs (design of the create path) |
| `components/system/sys_io/SYS_IO.MD` | IO contract, pin refs, locks, interrupt config, pin events |
| `components/system/sys_power/SYS_POWER.MD` | Power contracts, power manager (budget, sources, battery, response matrix) |
| `components/sys/sys_event/SYS_EVENT.MD` | Events (ported): IDs per contract, publish, notify stream, C listeners (inline / queued, device ON_EVENT or fn), rules event → action, router `0x07` |
| `components/system/sys_interface/SYS_INTERFACE.MD` | Inbound frame router, class registry, command responses, RX suspend, frame tap |
| `components/system/sys_actions/SYS_ACTIONS.MD` | Static / recorded (dynamic) actions, class `0x03`, boot and reset actions |
| `components/system/ble/SYS_BLE.MD` | BLE services/characteristics, runtime GATT changes, BLE provider, MTU |
| `components/system/sys_uart/SYS_UART.MD` | Console UART provider: `#R:<hex>` frame lines next to the log (test / fallback command link) |
| `components/system/sys_data_connector/SYS_DATA_CONNECTOR.MD` | Transport-agnostic bus: connectors, providers (framing, endpoint, peer), origin-addressed responses, dynamic frame limit, system connector protection, class `0x06` |
| `components/system/sys_buffers/SYS_BUFFERS.MD` | Item ring buffers used by transports |
| `components/system/sys_settings/SYS_SETTINGS.MD` | Named NVS records |
| `components/system/sys_project/SYS_PROJECT.MD` | Stored code: frame list in the `project` NVS partition, replayed at boot, read back for recovery, class `0x0A` |
| `components/sys_errors/SYS_ERRORS.MD` | Error chain, pool, maps, handler, logging, binary packet |
| `components/codecs/CODECS.MD` | Decoder tables per class, packet byte ranges, adding a class |
| `components/VM/VM.MD`, `VM_EXEC.MD` | VM objects, loader, execution, events |
| `data-structures/AGENTS.md` + `auto-annotations/*/*-annotations.md` | `//@` annotation grammar and the JSON generators (device, contracts, settings, VM) |

Ported to the new tree (`components/sys/<module>`, found via `EXTRA_COMPONENT_DIRS`): `components/core/cmd/CMD.MD` (the command handler type, `cmd_reply`, `ERR_CMD_*`: what the router and the device registry both build on, so a device compiles without the router), `sys_device` → `components/sys/sys_device/SYS_DEVICE.MD`, `sys_io` → `components/sys/sys_io/SYS_IO.MD` (the IO contract as u16 device ops), `sys_i2c` → `components/sys/sys_i2c/SYS_I2C.MD`, `sys_uart` → `components/sys/sys_uart/SYS_UART.MD`, `sys_ble` → `components/sys/sys_ble/SYS_BLE.MD` (replaces legacy `ble` + `sys_buffers`), `sys_connector` → `components/sys/sys_connector/SYS_CONNECTOR.MD` (outbound streams; replaces `sys_data_connector`), `sys_power` → `components/sys/sys_power/SYS_POWER.MD` (the POWER_MONITOR `0x04` and POWER_VREG `0x05` contracts only; no power manager yet), `sys_actions` → `components/sys/sys_actions/SYS_ACTIONS.MD` (stored command lists by u8 ID with a name label: record via the router tap, upload / read the full image, run on a router task). Transport IDs: `transport_e` in `components/sys/sys_connector/sys_connector.h`. Ported devices: `components/devices/device_<chip>/DEVICE_<CHIP>.MD` (GPIO ESP, PCA9685, TPS55289). Remote commands: `components/core/router/ROUTER.MD` (replaces `sys_interface` + `codecs`; root keys and link IDs in `router_ids.h`). New error component: `components/core/errors/ERRORS.MD` (replaces `sys_errors`; module IDs in `err_modules.h`).

No doc yet: `sys_hbridge` (ask the user before writing one, conventions.md §8).

Remaining source material: the legacy firmware skill, backed up at `../_agents_backup_2026-09-22/runit-legacy-skill/SKILL.md` (relative to `_SOFTWARE`).

## Quick build

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 | Out-Null; idf.py -C C:\Users\lukasz\Documents\runIT\_SOFTWARE build
```

After editing any `Kconfig`: run `idf.py reconfigure`. If you changed an option's `default`, run `idf.py refresh-config --policy=kconfig` instead. See [build.md](build.md) §3.
