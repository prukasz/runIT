# runIT — durable project notes

Index of knowledge that is not obvious from the code. Detail lives in the linked files; keep this short and update it when a note goes stale.

## Where things are written down

- Vision, hardware, current progress: `.claude/skills/runit-root/` (`VISION.md`, `HARDWARE.md`, `PROGRESS.md`)
- Firmware architecture, devices, errors, build: `.claude/skills/runit-esp/`
- App (old, reference only): `.claude/skills/runit-app/`; its paths and the `data-structures/` it names are stale
- New app and VM engine: `runit-app/` (`AGENTS.md`, `src/engine/ENGINE.md`)
- Device descriptor format: `devices-format.md`
- Block canvas concept: `board-canvas-concept.md`
- Wire format shared by firmware and app: `templates/AGENTS.md`
- Per-module design notes: the `*.MD` next to each component

## Gotchas

- **ESP-IDF build on the author's machine:** the generic `export.ps1; idf.py build` from the docs fails. IDF is split across `C:\esp\v6.1\esp-idf` and `C:\Espressif\tools`, so the build needs `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH` and `IDF_PYTHON_CHECK_CONSTRAINTS=no` set first. Working command: `.claude/skills/runit-esp/build.md`.
- **Expected CMake warnings:** about a dozen warnings on `sys_errors` include dirs are the deliberate component-cycle break described in `SYS_ERRORS.MD`. They are not a problem.
- **Layering:** `core` → `sys` → `devices`; `main` composes them. Components are found via `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt`.
- **App UI** shows names, never `obj-…` IDs.
- **`data-structures/` is legacy.** The app reads `generated/` (the bundle `templates/generate_bundle.py` copies from `components/`); the firmware C in `components/` outranks any descriptor. Block descriptors publish `state_len` (compared with the C by `crosscheck.py`), every device variant names its `op`, and the Kconfig limits are in `generated/common/limits.json` (`#@limit` markers, `generate_limits.py`). A device block's `op` on the wire is the full u16 `(contract_id << 8) | op` (`0xFF00 | op` for a device's own command), not the descriptor's bare `op`: see `runit-app/src/engine/ENGINE.md`.

## Decisions

Record new architecture decisions as short ADRs in `docs/adr/` (one file each: context, decision, consequence).
