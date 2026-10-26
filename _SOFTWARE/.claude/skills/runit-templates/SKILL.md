---
name: runit-templates
description: Skill for runIT's descriptors and generators in templates/ - the JSON Schemas (device, contract, board, VM block, errors, enums, limits, hardware part), the generate_*.py scripts, the runit-app/generated/ bundle, the hardware library (library/parts) and sync.sh. Load for any change to a device_*.json, contract_*.json, board.json, block_*.json, errors.json, an errors_<module>.json (error messages) or a library part.json, to a schema, to a marked enum or define group, to a #@limit Kconfig option, or when runit-app/generated/ is out of date.
---

# runIT — Templates Skill (descriptors and generators)

Session context: `runit-root` skill ([SKILL.md](../runit-root/SKILL.md)). Firmware code: `runit-esp` skill ([SKILL.md](../runit-esp/SKILL.md)).

**The `templates/AGENTS.md` file is the source of truth.** It holds the descriptor table, the generators, the `runit-app/generated/` bundle, the publishing markers (`//#ref-enum`, `#@limit`), the core-versus-scanned rule and the synchronization. The hardware library (`library/parts/<id>/part.json` + `part.svg`, `part.schema.json`, checked by `check_library.py`) is described in [library/LIBRARY.md](../../../library/LIBRARY.md). Read it before changing anything in `templates/`, and follow it.

Every change here runs `sh templates/sync.sh` before it is committed, and the commit leaves `sync.sh --check` green. `runit-app/generated/` and the generated `*.json` files are never edited by hand: change the source (a descriptor next to its code, a marker, a Kconfig option or a schema) and regenerate.
