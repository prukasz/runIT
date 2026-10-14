# Templates: schemas and generators

`templates/` holds the JSON Schemas of every descriptor the firmware and the app share, and the generators that publish what the C code owns.

- Every change in this folder is followed by an edit of this file, if it is needed.
- Do not add references to `legacy/` unless the user asks for them.

## Descriptors written by hand (next to the code)

| Descriptor | Where | Schema |
|---|---|---|
| Device | `components/devices/device_<name>/device_<name>.json` | `device.schema.json` |
| Contract | `components/sys/sys_<name>/contract_<name>.json` (also `core/router`, `vm/vm_core`) | `contract.schema.json` |
| Board | `components/app/runit/board.json` | `board.schema.json` |
| VM block | `components/vm/vm_blocks/**/block_<name>.json` (one per block type of `vm_block_ids.h`; `crosscheck.py` lists any without one) | `vm-block.schema.json` |

Each descriptor names its schema in `$schema`. A descriptor is found by its file name pattern (`device_*.json`, `contract_*.json`), so a new device or contract needs no registration.

## Descriptors generated from C

| Generator | Output | Schema |
|---|---|---|
| `generate_enums.py` | `generated/common/enums.json` | `enums.schema.json` |
| `generate_limits.py` | `generated/common/limits.json` | `limits.schema.json` |
| `generate_errors.py` | `components/core/errors/errors.json` | `errors.schema.json` |

Generators are named `generate_<what>.py`, take `--check` and share their helpers in `_common.py`. Generated files are not edited by hand. Re-run them after changing a marked enum or define group (`generate_enums.py`), a `#@limit` Kconfig option or its value in `sdkconfig` (`generate_limits.py`) or an `ERR_MODULE_DEFINE` / `ERR_*_MAP` (`generate_errors.py`), or just run the synchronization below.

`generate_errors.py` reads `ERR_MOD_*` from `components/core/errors/err_modules.h`, and the `ERR_MODULE_DEFINE` and `ERR_*_MAP` macros from the headers under `components/`.

## The `generated/` bundle (what the app reads)

`generate_bundle.py` copies the descriptors from `components/` into `generated/` (repository root), so the app reads one place and never the source tree:

| Folder | Content | Taken from |
|---|---|---|
| `generated/devices/` | `device_*.json` | `components/devices/*/` |
| `generated/contracts/` | `contract_*.json` with `scope` `device` (or none) | `components/sys/*/` and others |
| `generated/services/` | `contract_*.json` with `scope` `system` | the same places |
| `generated/blocks/` | `block_*.json` | `components/vm/vm_blocks/**/` |
| `generated/common/` | `enums.json` (written by `generate_enums.py`), `limits.json` (written by `generate_limits.py`), `errors.json`, `board.json` | generated / `components/core/errors/`, `components/app/runit/` |

A copy is the source with `$schema` rewritten to point at `templates/` from its folder. The script also deletes copies whose source is gone. A new device or contract appears in the bundle by its file name; there is no list to edit. The source stays in `components/`; edit it there, never in `generated/`.

### What is published: only what is marked

A private enum never becomes wire vocabulary. Two markers, directly above the code, publish it:

```c
//#ref-enum @alias IO mode
typedef enum {
  SYS_IO_MODE_ADC = 7,  //@alias ADC @description Measures the voltage on the pin
  SYS_IO_MODE_COUNT,    // sentinel: left out
} sys_io_mode_e;

//#ref-defines router_key @alias Command targets @prefix ROUTER_KEY_
#define ROUTER_KEY_SYS 0x01u  //@alias System @description The system router
```

- `@alias` is required on the marker and on every member. It is the name a user sees. Nothing is derived from a C identifier or from a plain comment, and text outside a tag is an error.
- `@description` is optional. `@wire-size <bytes>` and `@mem-size <bytes>` pass through as `wire_size` / `mem_size`.
- A value is a literal, an expression of earlier members, a `#define`, or a `CONFIG_*` option read from `sdkconfig`. `*_CNT` and `*_COUNT` sentinels are left out.
- A member name belongs to exactly one published group; `symbols` maps it back to that group.

A device or contract field of type `enum` names a published group (`"enum": "sys_i2c_bus_e"`); the app reads its members from `enums.json`.

### Limits: what the board enforces

An option a program or a frame can run into (pins per block, the program arena, nesting depth, the largest frame) is published with a comment directly above its `config` line in a Kconfig:

```
#@limit @alias Inputs per block @unit pins
config VM_BLOCK_MAX_IN
    int "Maximum input accessors per block"
    default 16
    help
        Maximum number of input accessors allowed for a single VM execution block.
```

`generate_limits.py` writes `generated/common/limits.json` with the value the firmware is built with (`sdkconfig`, like `generate_enums.py`) and the first paragraph of the `help` as the description. `@alias` is required, `@unit` optional; nothing unmarked is published (task priorities and stack sizes are not limits a program can hit). Change the value (`idf.py menuconfig`, then `idf.py reconfigure`) and run `sh templates/sync.sh`. The app reads the file; what it derives (the payload of a frame is `CONFIG_ROUTER_FRAME_MAX` less `[seq][flags]` and the two keys of a VM command) is its own arithmetic, checked against the C by its tests.

## Core versus scanned

The interpretation of a contract (what an operation does) lives in code and is core. Everything the app shows, validates or encodes comes from these descriptors, so a new device, contract, enum member or error needs a descriptor, not a code change in the app.

## Synchronization

```sh
sh templates/sync.sh            # regenerate enums.json and errors.json, refresh generated/, then validate everything
sh templates/sync.sh --check    # CI: fail when a generated file or a copy in generated/ is out of date, then validate
```

Run it after any change to a descriptor, a schema, a marked enum or define group, or an error map; the commit should leave it green. `sync.sh` runs `generate_enums.py`, `generate_limits.py`, `generate_errors.py`, `generate_bundle.py`, `validate_schemas.py` and `crosscheck.py`. `crosscheck.py` checks what no single schema can: a device's `contracts` name existing contracts; a VM block's `wire_id` is unique, below `VM_BLOCK_TYPES_MAX` and equals `VM_BLK_<ID>` of `components/vm/vm_core/block/vm_block_ids.h`, and its `state_len` equals the `.state_len` of its `VM_BLOCK_TYPE_DEFINE` (a `sizeof` that a `_Static_assert` pins to a number; 0 when the definition names none), and the `idle` options of a device block are 0 or an op code (`(contract_id << 8) | op`, or `0xFF00 | op` for a device's own command); `device.contract` / `device.device_type` exist; a variant's `op` is a command of that contract and its `args` keys are arguments of that command; `fixed` / `default` / condition values are members of the enum group, options or contract choice they refer to; `{name}` in a header or body is a field, argument, pin, `device` or `variant`; a condition names a field of the block, `device.<config id>` of the block's `device_type` (a contract block has no device config to name) or a variant. Block types without a descriptor are listed, not failed. The validator checks every descriptor (devices, contracts, VM blocks, errors, board, enums, limits) against its schema (JSON Schema 2020-12) and that each enum a descriptor names (`"enum"` of a field, `enum_ref` of an error field) is published in `enums.json`. `sync.sh` uses the Python of the repository's `.venv` (agents run it that way too), falling back to `python`; `PYTHON=<interpreter>` overrides it. The validator needs `jsonschema` in that Python (`.venv/Scripts/python.exe -m pip install jsonschema`).
