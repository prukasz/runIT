# runIT — _SOFTWARE

Plug-and-play framework for hobbyists: ESP32-S3 firmware (ESP-IDF, FreeRTOS), a PLC-style VM uploaded over BLE, and an app (remote, debugger, configurator, block IDE). The app is being written in `runit-app/` (so far the VM engine and the BLE link, read its `AGENTS.md`); it reads what the firmware publishes in `generated/`.

## Start of session

Load the `runit-root` skill first. It holds the vision, hardware and repo map. Then load the skill for the task:

| Task touches | Skill | Code root |
|---|---|---|
| Firmware (C, devices, VM, BLE, errors, build/flash) | `runit-esp` | `components/`, `main/` |
| Descriptors, schemas and their generators | `runit-templates` | `templates/` |
| The app (VM engine, BLE link) | none yet: `runit-app/AGENTS.md`, `runit-app/src/engine/ENGINE.md`, `runit-app/src/backend/BACKEND.md` | `runit-app/` |

Durable notes and gotchas: `.claude/memory.md`.

Code graph: `graphify-out/` (firmware C, headers preprocessed, plus the docs). For structure questions (callers, module links, impact of a change) query it first: `graphify query "…"`, `explain "X"`, `path "A" "B"`, `affected "X"`. Grep is for exact strings. Rebuild: `$(cat graphify-out/.graphify_python) tools/graphify_pp.py` (about 30 s; needs `build/compile_commands.json`). A post-commit hook runs it after commits that touch `components/`, `main/` or the docs; new or changed docs have no semantic results until `/graphify . --update`. Never run plain `graphify update` or `graphify hook install` here: they drop the preprocessed headers.

## Rules

- **Docs follow every change.** Any change to code, wire format, descriptors, build or layout is followed, in the same change and without being asked, by an edit of every document it makes stale: the component's `*.MD` and `AGENTS.md`, the skills in `.claude/skills/` (`runit-root`, `runit-esp`, `runit-templates`), `templates/AGENTS.md`, this file and `.claude/memory.md`. Docs describe the current state only: no history, no changelog, no references to legacy code.
- Layers: `core` → `sys` → `devices` → `vm`; `components/app/runit` composes them. A component depends only on earlier layers.
- Build commands for this machine are in `runit-esp` (`build.md`); the generic ESP-IDF command fails here.
- An app UI shows names, never `obj-…` IDs.
- Descriptors (`device_*.json`, `contract_*.json`, `errors.json`) follow `templates/*.schema.json`. `build/` and `graphify-out/` are not edited by hand.
- Match the surrounding code style (`.clang-format` for C).
