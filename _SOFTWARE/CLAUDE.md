# runIT — _SOFTWARE

Plug-and-play framework for hobbyists: ESP32-S3 firmware (ESP-IDF, FreeRTOS), a PLC-style VM uploaded over BLE, and a React/TypeScript app (remote, debugger, configurator, block IDE).

## Start of session

Load the `runit-root` skill first. It holds the vision, repo map and current progress (`.claude/skills/runit-root/PROGRESS.md`). Then load the skill for the task:

| Task touches | Skill | Code root |
|---|---|---|
| Firmware (C, devices, VM, BLE, errors, build/flash) | `runit-esp` | `components/`, `main/` |
| App (React/TS, BLE client, canvas) | `runit-app` | `app/` |
| Wire format shared by both | `templates/AGENTS.md` | `templates/` |
| New app and VM engine (written from scratch; `app/` and `legacy/` are reference only) | `runit-app/AGENTS.md`, `runit-app/src/engine/ENGINE.md` | `runit-app/` |

Durable notes and gotchas: `.claude/memory.md`.

## Rules

- Layers: `core` → `sys` → `devices`; `main` composes them. A component depends only on earlier layers.
- Build commands for this machine are in `runit-esp` (`build.md`); the generic ESP-IDF command fails here.
- In the app UI show names, never `obj-…` IDs (use `pathLabel` / `nameIds`).
- Descriptors (`device_*.json`, `contract_*.json`, `errors.json`) follow `templates/*.schema.json`. `build/` and `graphify-out/` are not edited by hand.
- Match the surrounding code style (`.clang-format` for C).
