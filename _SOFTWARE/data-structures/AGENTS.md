# Auto-annotations

## Purpose

Generate the descriptors the web app loads (JSON + Markdown guides) from annotated C. C is the only source; generated files are output only. The app reads nothing else and builds blocks, devices, settings and forms from it.

- `enums.json`: the published enums.
- `contracts/`: router keys and ops, device contracts, events, wire structs.
- `errors/`: every error tag (module, code, severity, payload layout) to read an error record.
- `devices/`: one descriptor and guide per device type.
- `vm/blocks/`: one descriptor and guide per VM block, plus the index.
- `auto-annotations/`: the generators, their tests and the annotation grammar.
- `schema/`: JSON schemas of the **old** (v1) files. They do not describe the new outputs yet.

## Generators in use

Each is standalone and self-contained (no arguments except `--check`), reads the others' output live, and has unit tests that copy `components/` to a temp tree, break one line and expect a refusal.

| Generator | Grammar | Output |
|---|---|---|
| `enums/generate-enums.py` | `//#ref-enum` (a member may take its value from `CONFIG_*`, resolved from `sdkconfig`) | `enums.json` |
| `errors/generate-errors.py` | the `ERR_*_MAP` X-macros (`errors/` docstring: payload field comments `/*@enum-ref e*/`, `/*@id kind*/`) | `errors/errors.generated.json` |
| `contracts/generate-contracts.py` | `contracts/contract-annotations.md` | `contracts/contracts.generated.json` |
| `vm/generate-vm-blocks.py` | `vm/vm-block-annotations.md` | `vm/blocks/*.block.json`, `*.md`, index |
| `device/generate-devices.py` | `device/device-annotations.md` | `devices/<id>.generated.json`, `<id>.md` |

`auto-annotations/generate-all.py` runs them in that order (`--check` changes nothing and exits 1 naming the out-of-date files; `--test` also runs all tests). Run it after changing any annotated header, a Kconfig option a descriptor reads, or the ESP-IDF version.

```powershell
python data-structures/auto-annotations/generate-all.py [--check] [--test]
```

## Rules

- Read the grammar file of a generator before changing its annotations, the headers it reads, or the generator.
- Do not edit generated files (`*.generated.json`, `vm/blocks/*`, `devices/*`, `enums.json`, `contracts/*`); change the C annotations or the generator and regenerate.
- Every name a user sees needs an explicit `@alias`; the generators fail on a missing or unknown tag. Descriptions are client-facing: purpose and use, not implementation.
- `//#ref-enum` publishes an enum; `$SYMBOL` must resolve to exactly one member of a published enum.
- Keep annotations next to the C they describe. A new tag needs documented grammar, generator support and a test (including a refusal).
- Generated files are written with LF on every platform.

## Not yet on the new grammar

Their generators still read the pre-2026-10 firmware and are **not** run by `generate-all.py` (listed as `LEGACY_STEPS` there): `vm/generate-vm-model.py`, `vm/generate-vm-program.py`, `settings/generate-settings.py`, `board/generate-board.py`, `streams/generate-streams.py`. Rewrite each the way the four above were before using it; their old outputs and schemas are stale. Also not ported to the device generator: ports/pins CSV and IC symbols (`device_symbol*.py`, `pins-csv-guide.md`, whose `device_symbol_svg.py` has a non-ASCII syntax error), the limits header and canvas layouts. `board/board-terminals.json` is hand-written electrical data of the runIT board.
