---
name: runit-root
description: Entry point for any new session in the runIT repo. Gives project vision, repo map, which sub-skill to load (runit-esp for firmware, runit-app for the phone/PC app), and tracks progress of runit-esp32 and runit-app. Load first when starting work, when asked "where are we / what's next", or when a task spans firmware and app.
---

# runIT — Root Skill (session entry point)

## 1. Start of session checklist

1. Read [VISION.md](VISION.md) once if the project concept is unfamiliar (what runIT is, layers, goals).
   Read [HARDWARE.md](HARDWARE.md) for board capabilities, onboard chips and their device IDs.
2. Read [PROGRESS.md](PROGRESS.md) — current state, in-flight work and next steps for both sub-projects.
3. Run `git status` / `git log --oneline -10` — active branch and uncommitted work may be ahead of PROGRESS.md.
4. Route to the sub-skill for the task:

| Task touches | Load skill | Code root |
|---|---|---|
| Firmware (C, ESP-IDF, FreeRTOS, devices, VM, BLE, errors) | `runit-esp` | `components/`, `main/` |
| App (React/TS, BLE client, canvas, remote, configurator) | `runit-app` | `app/` |
| Wire format / JSON descriptors shared by both | `data-structures/AGENTS.md` | `data-structures/` |

`runit-esp` is complete except `vm.md`; `runit-app` is a stub. The old skills they replace are backed up outside the repo in `../_agents_backup_2026-09-22/` — not loaded automatically.

## 2. Project in one paragraph

runIT is a plug-and-play framework for hobbyists spanning hardware, firmware and an app. **runIT-ESP32** (FreeRTOS/ESP-IDF, ESP32-S3) exposes all hardware through device-agnostic **contracts** implemented by per-chip **adapters**, streams interface-agnostic byte **packets** (BLE now, Wi-Fi/LoRa later), enforces a nested **error chain**, and runs a PLC/Node-RED-style **VM** (objects, accessors, blocks) uploaded live without reflashing, with a bidirectional **remote-control** mode. **Features** (servo, H-bridge, …) build higher-level functions on devices. The **runIT board** integrates PD/LiPo power, programmable regulators, H-bridges, 20 V ADC and a PWM expander. The **runIT app** is remote, debugger, configurator and block IDE — the end user never writes code.

## 3. Repo map (`_SOFTWARE/`)

| Path | Contents |
|---|---|
| `main/` | ESP-IDF entry (`main.c`) |
| `components/system/` | Core system modules: `sys_device`, `sys_io`, `sys_power`, `sys_i2c`, `ble`, `sys_interface`, `sys_data_connector`, `sys_actions`, `sys_buffers`, `sys_event`, `sys_hbridge`, `sys_settings` — each with a `*.MD` doc |
| `components/devices/` | Chip drivers + adapters (`device_<chip>/driver_*.c`, `adapter_*.c`) |
| `components/features/` | High-level features (servo, hbridge, registry) |
| `components/VM/` | Flow-language VM (`core/`, `blocks/`, `VM.MD`, `VM_EXEC.MD`) |
| `components/codecs/` | Packet decoders/encoders (`CODECS.MD`) |
| `components/sys_errors/` | Error chain, codes, logging (`SYS_ERRORS.MD`) |
| `components/runit/` | Board config/defs, startup |
| `components/utils/` | Static-allocation `R_*` macros, Kconfig |
| `data-structures/` | C-annotation → JSON generators (`auto-annotations/`), schemas, `*.generated.json` consumed by the app (see its `AGENTS.md`) |
| `app/` | runIT app: Vite + React 19 + TypeScript + Tailwind; `src/backend/` (ble, packetPack, stream) |

## 4. Commands

```powershell
# Firmware build (ESP-IDF v6.1 via EIM profile; details, Kconfig reconfigure, flash: runit-esp/build.md)
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 | Out-Null; idf.py -C C:\Users\lukasz\Documents\runIT\_SOFTWARE build
# App
cd app; npm run dev      # / npm run build, npm run lint
# Regenerate JSON descriptors — see data-structures/AGENTS.md
```

Only ESP-IDF v6.1 is installed (`C:\esp\v6.1\esp-idf`, matching the root `CMakeLists.txt` default). Its `export.ps1` does not work with the EIM install. Use the profile script above.

## 5. Progress tracking rules

- [PROGRESS.md](PROGRESS.md) is the single progress log for both sub-projects. Update it at the end of any session that finishes, starts or blocks a work item.
- Keep entries short: status, one-line description, pointer to file/doc. Move finished items to "Done" with the date (YYYY-MM-DD).
- Don't duplicate design detail here — link to the component `.MD` or app design doc instead.
- When the repo layout changes (new component, device, app module), update the repo map in §3.
