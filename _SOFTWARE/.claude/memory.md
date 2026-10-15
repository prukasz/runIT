# runIT — durable project notes

Index of knowledge that is not obvious from the code. Detail lives in the linked files; keep this short and update it when a note goes stale.

## Where things are written down

- Vision, hardware, repo map: `.claude/skills/runit-root/` (`VISION.md`, `HARDWARE.md`)
- Firmware map (links every component's `*.MD` / `AGENTS.md`), architecture, conventions, errors, build and tests: `.claude/skills/runit-esp/`
- The app (`runit-app/`): `AGENTS.md`, `src/engine/ENGINE.md` (VM engine), `src/backend/BACKEND.md` (the BLE link, the stored project)
- Device descriptor format: `devices-format.md`
- Block canvas concept: `board-canvas-concept.md`
- Descriptors, schemas and generators: `templates/AGENTS.md`
- Per-module design notes: the `*.MD` next to each component

## Gotchas

- **ESP-IDF build on the author's machine:** the generic `export.ps1; idf.py build` from the docs fails. IDF is split across `C:\esp\v6.1\esp-idf` and `C:\Espressif\tools`, so the build needs `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH` and `IDF_PYTHON_CHECK_CONSTRAINTS=no` set first. Working command: `.claude/skills/runit-esp/build.md`.
- **CMake warnings:** only IDF's own (`esp_wifi` / `wpa_supplicant` private include dirs). Any project warning is new and must be fixed.
- **Layering:** `core` → `sys` → `devices` → `vm`; `components/app/runit` composes them. Components are found via `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt`.
- **Stored actions:** IDs 3 to 6 are the firmware's (resume, suspend, reset, hard reset); a project section runs at every start, so an action that waits for a call is stored with the actions router, not as a section (SYS_PROJECT.MD).
- **Hardware tests over BLE** (`runit-esp/scripts/ble_suite.py`, `build.md` §6): after a GATT change Windows reports no disconnect and every write fails until the link is reopened; opening the console port can reset the board.
- **App UI** shows names, never `obj-…` IDs.
- **Descriptors:** `templates/sync.sh` publishes them to `generated/` (the bundle `templates/generate_bundle.py` copies from `components/`); the app (`runit-app/`) reads them. The firmware C in `components/` outranks any descriptor. Block descriptors publish `state_len` (compared with the C by `crosscheck.py`), every device variant names its `op`, and the Kconfig limits are in `generated/common/limits.json` (`#@limit` markers, `generate_limits.py`). A device block's `op` on the wire is the full u16 `(contract_id << 8) | op` (`0xFF00 | op` for a device's own command), not the descriptor's bare `op`.

## Decisions

Record new architecture decisions as short ADRs in `docs/adr/` (one file each: context, decision, consequence).
