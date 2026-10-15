---
name: runit-root
description: Entry point for any new session in the runIT repo. Gives project vision, hardware, repo map, which sub-skill to load (runit-esp for firmware, runit-templates for descriptors; the app in `runit-app/` has its own docs) and the rule that every change updates the docs. Load first when starting work, when asked "where are we / what's next", or when a task spans firmware and app.
---

# runIT — Root Skill (session entry point)

## 1. Start of session checklist

1. Read [VISION.md](VISION.md) once if the project concept is unfamiliar (what runIT is, layers, goals).
   Read [HARDWARE.md](HARDWARE.md) for board capabilities, onboard chips and their device IDs.
2. Run `git status` / `git log --oneline -10`: the active branch and the uncommitted work are the current state. What is still open is written in the component docs (each `*.MD` has its "Open" items).
3. Route to the sub-skill for the task:

| Task touches | Load skill | Code root |
|---|---|---|
| Firmware (C, ESP-IDF, FreeRTOS, devices, VM, BLE, errors) | `runit-esp` | `components/`, `main/` |
| JSON descriptors, schemas and their generators | `runit-templates` | `templates/` |

`runit-esp` maps the firmware: every component has its own `*.MD` (API, wire format) and `AGENTS.md` (rules for changing it), and the skill links them. The app is written in `runit-app/` (VM engine and BLE link so far; start at its `AGENTS.md`); it reads what the firmware publishes as descriptors in `generated/`.

## 2. Project in one paragraph

runIT is a plug-and-play framework for hobbyists spanning hardware, firmware and an app. **runIT-ESP32** (FreeRTOS/ESP-IDF, ESP32-S3) exposes all hardware through device-agnostic **contracts** implemented by per-chip **devices**, takes **commands** and sends **streams** in one frame shape over any transport (UART and BLE now, Wi-Fi/LoRa later), enforces a nested **error chain**, and runs a PLC/Node-RED-style **VM** (objects, accessors, blocks) uploaded live without reflashing, with a bidirectional **remote-control** mode. What only one chip can do (servo, H-bridge driver, regulator protection) is that device's own operations, with a VM block of its own. Stored **actions** and **projects** replay commands on the board. The **runIT board** integrates PD/LiPo power, programmable regulators, H-bridges, 20 V ADC and a PWM expander. The **runIT app** is remote, debugger, configurator and block IDE — the end user never writes code.

## 3. Repo map (`_SOFTWARE/`)

| Path | Contents |
|---|---|
| `main/` | ESP-IDF entry (`main.c`, only `runit_start()`) |
| `components/core/` | Foundation, no hardware: `utils` (static RTOS macros), `errors` (error chain, pool, tags), `cmd` (command handler type), `router` (links, dispatch, root keys) |
| `components/sys/` | System modules and the contracts devices implement: `sys_device` (registry), `sys_io`, `sys_power`, `sys_hbridge` (contracts), `sys_power_manager`, `sys_event`, `sys_actions`, `sys_project`, `sys_connector`, `sys_log`, `sys_i2c`, `sys_ble`, `sys_uart` |
| `components/devices/` | One component per chip (`device_<chip>/`: create struct and own ops in the header, the driver in `device_<chip>.c`, descriptor `device_<chip>.json`) |
| `components/vm/` | The VM: `vm_core` (runtime, loader, telemetry; key `0x0B`) and `vm_blocks` (the block palette) |
| `components/app/runit/` | The application: boot steps, safe state, error policy wiring, board config and stream routing |
| `runit-app/` | The app, so far without UI: `src/engine` (VM engine: variables, accessors, blocks, formulas, compiler, run-time writes and telemetry, recovering a project from the stored program) and `src/backend` (the BLE link: commands, streams, reconnecting, stored records and the project) |
| `templates/`, `generated/` | Descriptor schemas, the generators and `sync.sh`; the published descriptors (`templates/AGENTS.md`) |

**Layers** (a group depends only on the ones before it): `core` → `sys` → `devices` → `vm`; `app/runit` composes them (router keys, boot order). Components are found through `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt`: a new component goes into the group of its layer, and no path is listed anywhere else.

## 4. Commands

```powershell
# Firmware build (ESP-IDF v6.1 via EIM profile; details, Kconfig reconfigure, flash: runit-esp/build.md)
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 | Out-Null; idf.py -C C:\Users\lukasz\Documents\runIT\_SOFTWARE build
# Regenerate and validate the JSON descriptors (templates/AGENTS.md)
sh templates/sync.sh
# Rebuild the code graph graphify-out/ (preprocessed C + docs from the semantic cache; post-commit hook does it too)
$(cat graphify-out/.graphify_python) tools/graphify_pp.py
```

Query the graph before grepping for structure: `graphify query "…"`, `graphify explain "X"`, `graphify path "A" "B"`, `graphify affected "X"`. Docs with no semantic result are listed by the script as `NOT IN GRAPH YET`: run `/graphify . --update` for them. Never use plain `graphify update` or `graphify hook install`: they drop the preprocessed headers.

Only ESP-IDF v6.1 is installed (`C:\esp\v6.1\esp-idf`, matching the root `CMakeLists.txt` default). Its `export.ps1` does not work with the EIM install. Use the profile script above.

## 5. Keeping the docs current

**Every change updates the docs it makes stale, in the same change, without being asked.** This covers code, wire format, descriptors, Kconfig, build steps, the repo layout and hardware facts. Check and edit:

| Doc | When |
|---|---|
| the component's `*.MD` and `AGENTS.md` | its API, behaviour, wire format, limits or rules changed |
| `runit-esp` skill (SKILL.md table, architecture.md, conventions.md, errors.md, build.md) | a component is added / removed / renamed, a flow or a cross-module rule changed, a build or test step changed |
| this skill (repo map §3, [HARDWARE.md](HARDWARE.md), [VISION.md](VISION.md)) | the layout, a board fact or the concept changed |
| `templates/AGENTS.md` | descriptors or schemas changed (then `sh templates/sync.sh`) |
| `CLAUDE.md`, `.claude/memory.md` | a rule, a gotcha or a pointer there changed |

Rules for every doc:
- It describes the current state only: no history, no changelog, no dates of changes, no "was / no longer", no references to legacy or removed code.
- One fact lives in one place; other docs link to it instead of copying it.
- Open work is written where it belongs (the component doc's "Open" items), not in a separate progress log.
- A link to a moved or deleted file is fixed in the same change.
