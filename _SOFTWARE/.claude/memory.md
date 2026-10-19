# runIT — durable project notes

Index of knowledge that is not obvious from the code. Detail lives in the linked files; keep this short and update it when a note goes stale.

## Where things are written down

- Vision, hardware, repo map: `.claude/skills/runit-root/` (`VISION.md`, `HARDWARE.md`)
- Firmware map (links every component's `*.MD` / `AGENTS.md`), architecture, conventions, errors, build and tests: `.claude/skills/runit-esp/`
- The app (`runit-app/`): `AGENTS.md` (the frame, the shared UI parts, the project, the desktop app), `src/engine/ENGINE.md` (VM engine), `src/backend/BACKEND.md` (the BLE link, the stored project)
- Block canvas concept: `board-canvas-concept.md`
- Descriptors, schemas and generators: `templates/AGENTS.md`
- Per-module design notes: the `*.MD` next to each component

## Gotchas

- **ESP-IDF build on the author's machine:** the generic `export.ps1; idf.py build` from the docs fails. IDF is split across `C:\esp\v6.1\esp-idf` and `C:\Espressif\tools`, so the build needs `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH` and `IDF_PYTHON_CHECK_CONSTRAINTS=no` set first. Working command: `.claude/skills/runit-esp/build.md`.
- **CMake warnings:** only IDF's own (`esp_wifi` / `wpa_supplicant` private include dirs). Any project warning is new and must be fixed.
- **Layering:** `core` → `sys` → `devices` → `vm`; `runit-esp/components/app/runit` composes them. Components are found via `EXTRA_COMPONENT_DIRS` in `runit-esp/CMakeLists.txt`.
- **Stored actions:** IDs 3 to 6 are the firmware's (resume, suspend, reset, hard reset); a project section runs at every start, so an action that waits for a call is stored with the actions router, not as a section (SYS_PROJECT.MD).
- **Hardware tests over BLE** (`.claude/skills/runit-esp/scripts/ble_suite.py`, `build.md` §6): after a GATT change Windows reports no disconnect and every write fails until the link is reopened; opening the console port can reset the board.
- **The app's backend on the board:** `npm run test:hw` in `runit-app/` (BACKEND.md "Tests"). Windows keeps a closed client's GATT session until its supervision timeout, so the board logs a lost link (`safe state (reason 3)`) about 3 s after the client closed it; a test that waits for it needs a window of seconds.
- **App UI** shows names, never `obj-…` IDs, and is built from `runit-app/src/ui/components` only: `tools/check-ui.mjs` fails a raw `<input>`, `<select>`, text `<button>` or a component's class in a screen (in `npm test`, and a PostToolUse hook in `.claude/settings.json` after each edit); `ui-raw: <reason>` excuses one line.
- **The board takes one BLE client:** while the user's `npm run app` (or a browser tab) is connected, a second app window cannot connect. Opening the console port (UART suites) resets the board, so the app's link drops and comes back.
- **Electron from a VS Code terminal:** `ELECTRON_RUN_AS_NODE=1` is set there, so a bare `electron .` starts as Node; `electron/start.mjs` drops it. One instance per profile (two would lose localStorage); the dev app has its own profile. `npm run app:dist` can fail with `EPERM … rename win-unpacked.tmp` under `Documents` (Controlled folder access): build with `-- -c.directories.output=<path elsewhere>`.
- **Is the program running:** `VM STATUS` (op `0x0A`) answers the run mode, whether a program is loaded and complete, the fault latch and the passes. `next_block` names the block the next `NEXT` runs in block mode. The program clock holds still while the VM is stepped, so each pass asked for adds SPEED's `step_ms` (default 10) or timing blocks never fire. After a requested pass (`ONCE`) the mode is `VM_RUN_FROZEN` with the scan mode still selected, so a client keeps the stepping it selected. Nothing reports whether the devices are suspended after a safe state.
- **App and descriptors:** the app's dev server caches the imported `runit-app/generated/` JSON: after `sync.sh` changes it, `touch` the changed files (or restart `npm run dev`) and reload the page, or the old descriptor stays on screen.
- **App and the board after a hot reload:** an edit of `App.tsx` (or a hook it calls) remounts it with a new link object while the board stays connected to the old one: reads then fail with "The board is not connected". Reload the page before checking anything on the board.
- **Descriptors:** `templates/sync.sh` publishes them to `runit-app/generated/` (the bundle `templates/generate_bundle.py` copies from `runit-esp/components/`); the app (`runit-app/`) reads them. The firmware C in `runit-esp/components/` outranks any descriptor. Block descriptors publish `state_len` (compared with the C by `crosscheck.py`), every device variant names its `op`, and the Kconfig limits are in `runit-app/generated/common/limits.json` (`#@limit` markers, `generate_limits.py`). A device block's `op` on the wire is the full u16 `(contract_id << 8) | op` (`0xFF00 | op` for a device's own command), not the descriptor's bare `op`.

## Decisions

Record new architecture decisions as short ADRs in `docs/adr/` (one file each: context, decision, consequence).
