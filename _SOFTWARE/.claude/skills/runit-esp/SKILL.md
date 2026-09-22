---
name: runit-esp
description: Firmware skill for runIT-ESP32 (ESP-IDF v6.1, FreeRTOS, ESP32-S3) — contracts, device adapters, sys_* modules, error chain, BLE/data connector, VM and features. Load for any work under components/ or main/, and for building, configuring (Kconfig) or flashing the firmware.
---

# runIT-ESP32 — Firmware Skill

Session context, hardware and progress: `runit-root` skill ([SKILL.md](../runit-root/SKILL.md), [HARDWARE.md](../runit-root/HARDWARE.md), [PROGRESS.md](../runit-root/PROGRESS.md)).

Component `*.MD` files are the source of truth for each module's API. This skill only holds routing, rules that apply across modules, and recipes.

## Files — read on demand

| File | Read when |
|---|---|
| [build.md](build.md) | Building, Kconfig changes (reconfigure / refresh-config), flashing, serial capture, clangd |
| [errors.md](errors.md) | Raising, propagating or handling errors; owners; severity |
| [architecture.md](architecture.md) | Layers and allowed dependencies, boot sequence, command/error/event flows, agreed design, open findings |
| [conventions.md](conventions.md) | Writing or changing any firmware code: static RTOS objects (`utils.h`), constants vs Kconfig, no legacy leftovers, validate-once, naming, `//@` annotations, comments/docs |
| [devices.md](devices.md) | Adding or changing a device: driver/adapter/decoder anatomy, checklist, rules, per-device state |
| vm.md | Adding a VM block (⏳ not written yet; use `VM.MD` / `VM_EXEC.MD`) |

## Module docs — read the one you touch

| Doc | Covers |
|---|---|
| `components/system/sys_device/SYS_DEVICE.MD` | Device registry, lifecycle ops, dispatch macros, per-instance error handling |
| `components/system/sys_io/SYS_IO.MD` | IO contract, pin refs, locks, interrupt config, pin events |
| `components/system/sys_power/SYS_POWER.MD` | Power contracts, power manager (budget, sources, battery, response matrix) |
| `components/system/sys_event/SYS_EVENT.MD` | Events: publish, subscriptions (inline / queued), routes, actions, class `0x09` |
| `components/system/sys_interface/SYS_INTERFACE.MD` | Inbound frame router, class registry, command responses, RX suspend, frame tap |
| `components/system/sys_actions/SYS_ACTIONS.MD` | Static / recorded (dynamic) actions, class `0x03`, boot and reset actions |
| `components/system/ble/SYS_BLE.MD` | BLE services/characteristics, runtime GATT changes, RX/TX to data connectors |
| `components/system/sys_buffers/SYS_BUFFERS.MD` | Item ring buffers used by transports |
| `components/system/sys_settings/SYS_SETTINGS.MD` | Named NVS records |
| `components/sys_errors/SYS_ERRORS.MD` | Error chain, pool, maps, handler, logging, binary packet |
| `components/codecs/CODECS.MD` | Decoder tables per class, packet byte ranges, adding a class |
| `components/VM/VM.MD`, `VM_EXEC.MD` | VM objects, loader, execution, events |
| `components/features/FEATURES.MD` | Features (on hold) |
| `data-structures/AGENTS.md` + `auto-annotations/*/*-annotations.md` | `//@` annotation grammar and the JSON generators (device, contracts, settings, VM) |

No doc yet: `sys_i2c`, `sys_data_connector`, `sys_hbridge` (ask the user before writing one, conventions.md §8).

Remaining source material: the legacy firmware skill, backed up at `../_agents_backup_2026-09-22/runit-legacy-skill/SKILL.md` (relative to `_SOFTWARE`).

## Quick build

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 | Out-Null; idf.py -C C:\Users\lukasz\Documents\runIT\_SOFTWARE build
```

After editing any `Kconfig`: run `idf.py reconfigure`. If you changed an option's `default`, run `idf.py refresh-config --policy=kconfig` instead. See [build.md](build.md) §3.
