# Templates: schemas and generators

`templates/` holds the JSON Schemas of every descriptor the firmware and the app share, and the generators that publish what the C code owns.

- Every change in this folder is followed by an edit of this file, if it is needed.

## Descriptors written by hand (next to the code)

| Descriptor | Where | Schema |
|---|---|---|
| Device | `runit-esp/components/devices/device_<name>/device_<name>.json` | `device.schema.json` |
| Contract | `runit-esp/components/sys/sys_<name>/router/contract_<name>.json` (also `sys/sys_system/router/contract_sys.json`, `vm/vm_core/router`) | `contract.schema.json` |
| Board | `runit-esp/components/app/runit/board.json` (the PCB), `board_devkit.json` (a devkit, `CONFIG_RUNIT_BOARD_DEVKIT`: device 0 only, no power sources, monitor or rails) | `board.schema.json` |
| VM block | `runit-esp/components/vm/vm_blocks/**/block_<name>.json` (one per block type of `vm_block_ids.h`; `crosscheck.py` lists any without one) | `vm-block.schema.json` |
| Hardware part | `library/parts/<id>/part.json` with its drawing `part.svg` (the Devices canvas; [library/LIBRARY.md](../library/LIBRARY.md)). Not copied into `runit-app/generated/`: the app reads `library/` itself (`@library`) | `part.schema.json` |

Each descriptor names its schema in `$schema`. A descriptor is found by its file name pattern (`device_*.json`, `contract_*.json`), so a new device or contract needs no registration.

A field of type `bitmask` names its bits in `bits` (`{bit, label}`): the app offers only those, as chips. A bit the descriptor does not name is not a choice.

A field may say how the app can draw it in place of its own control, `"control"`: `slider` (a `number` with `min` and `max`, `step` optional: a slider in the range) or `switch` (a `choice` of exactly two options: the first is off, the second on). Mark only fields where it makes sense: a value with a natural range the user moves (a drive, a duty, a current limit), a real on / off choice (Off / On, No / Yes, Low / High). The app draws it only while the user allows it (App settings: Controls); `crosscheck.py` holds the control to its field. A contract field marked `slider` may leave out `min` / `max`: the range of a command's field belongs to the device, `"ranges": {"VOLTAGE.voltage_mV": {"min": 4000, "max": 20000, "step": 100}}` (key `COMMAND.field`) in the device descriptor, replaced for one board device by the same `ranges` in its `board.json` entry (the runIT board's rails: 4 – 20 V, 100 mV). The app takes the range for the field's min, max and step whatever the control, so a number field is limited by it too, and shows nothing above its max. A range that depends on the device's config says so in `by_config`: a list of `{when, min, max, scale?, step?}`, the first whose `when` holds (each config field equals its value; a field not set counts as its default) replaces the range, an end being a number or a config field's id times `scale` (the servo's `target.value_x10`: its `angle_min_deg`..`angle_max_deg` x 10 for an angle servo, 0..2550 with byte steering, else -1000..1000). A device's own command field with a `ranges` entry may be a slider without its own min / max, as a contract field may (`crosscheck.py`). A `number` field of a device's command may give `scale` (the wire value is the shown value times it): the app shows and takes the field in that unit, its min, max, step and `ranges` too, and encodes ×scale (rounded) and decodes ÷scale (`codec.ts`); the servo's position is degrees (or percent) in the app and tenths on the wire.

A field of type `channel` (a power monitor's `channel`) is a channel of the device the command goes to: the app lists them by name. The device names them in `provides.channels` (`count`, `labels`, `by_config` as for pins: INA3221 three, AP33772S one); a device whose channels are its pins (the DRV8962's bridges) gives none and the list comes from its `provides.pins`. `crosscheck.py` fails a device whose contract has a `channel` field and neither.

A pin field (`type: pin`) says the mode its pin must have in `requires` (`output`, `pwm`, `adc`, ...): the app lists only the pins of devices with that mode. An output that wants a PWM pin names `pwm`, not `output`.

**Every text a user reads is English**: the `alias`, `label`, `description` and `title` of a descriptor, the `@alias` / `@description` of a marker and the `message` of an error. The schemas' own `description` fields document the format for developers.

## Descriptors generated from C

| Generator | Output | Schema |
|---|---|---|
| `generate_enums.py` | `runit-app/generated/common/enums.json` | `enums.schema.json` |
| `generate_limits.py` | `runit-app/generated/common/limits.json` | `limits.schema.json` |
| `generate_errors.py` | `runit-esp/components/core/errors/errors.json` | `errors.schema.json`; its input `errors_<module>.json`: `error-texts.schema.json` |

Generators are named `generate_<what>.py`, take `--check` and share their helpers in `_common.py`. Generated files are not edited by hand. Re-run them after changing a marked enum or define group (`generate_enums.py`), a `#@limit` Kconfig option or its value in `sdkconfig` (`generate_limits.py`) or an `ERR_MODULE_DEFINE` / `ERR_*_MAP` (`generate_errors.py`), or just run the synchronization below.

`generate_errors.py` reads `ERR_MOD_*` from `runit-esp/components/core/errors/err_modules.h`, and the `ERR_MODULE_DEFINE` and `ERR_*_MAP` macros from the headers under `runit-esp/components/`.

### Errors: what a user reads, what an error is about

Each error module has `errors_<module name>.json` next to the header of its map, in the `errors/` folder of its component (`runit-esp/components/sys/sys_i2c/errors/errors_sys_i2c.json`, schema `error-texts.schema.json`), written by hand:

```json
{ "module": "ERR_MOD_SYS_IO", "errors": {
  "ERR_IO_PIN_IN_USE": { "message": "{pin} of {id} is already set up as {mode}", "fields": { "mode": { "enum_ref": "sys_io_mode_e" } } } } }
```

- **Every error has a `message`**: the template the app shows. `{field}` is a field of the error's payload; the app fills in the instance's name for an id field, the member's alias for an `enum_ref` field, the number with its unit otherwise. A message for a context error (severity NONE, `"context": true` in errors.json) says where (`In {id}`), not what went wrong.
- **A `hint`** (optional) says what the user can do about it, in a sentence or two (`"Set the pin's mode first (Devices: the pin's Mode, or Set mode), then use it."`), with `{field}`s as the message; the app shows it under the message. An error a user cannot act on (a broken frame of the app, a call of the firmware) has none. The C comment in the map is the error's `description`, for developers, and is not shown as the message's explanation.
- **What a field refers to** (`id_kind`, one of `id_kinds` in errors.json: `device`, `pin`, `channel`, `vm_object`, `vm_accessor`, `vm_block`, `vm_block_input`, `action`, ...) is what lets the app attach an error to an instance. The generator takes it from the field's name per module (`MODULE_ID_KINDS`), from a `/* @id kind */` note in the map, and from `fields` here, which wins (`"id_kind": null`: not an id). `id_kind_by` picks the kind by another field's enum member (`{ "field": "kind", "kinds": { "VM_REF_KIND_IN": "vm_accessor", ... } }`, or by number when that field has no `enum_ref`); `enum_ref` names a group of `enums.json`.
- **Scoped kinds** (`scope` in `id_kinds`: a pin or a channel is numbered within a device, a block pin within a block): the generator sets `of` to the one field of the scope's kind in the same error; with two such fields `of` must be given here.
- The generator fails on an error without a message, a `{name}` of the message or the hint that is not a field, an unknown kind, an `of` that is not a field of the scope's kind, a member that is not in the enum group, and an `errors_*.json` that matches no module. A new error tag needs its entry here in the same change.

### Command pins: `pin_state`

A command of a device contract whose field is of type `pin` (the device's own pin) says in which state the pin must be, `"pin_state": ["output"]` (any of the listed): `unconfigured` (no mode yet: the MODE write), `configured` (any mode: RESET), `input` / `output` (digital modes), `pwm`, `adc`, `dac`. The app lists only the pins in such a state (`contract.schema.json`; `requires` on a config's pin reference is another thing: the mode the referenced pin must be able to take). A device whose pins are in a mode right after reset says it in `provides.pins.default_mode` (`input` for the TCA6424A, `adc` for the ADS7128); without it a pin has no mode until a MODE write gives one (the ESP GPIO). A device with no MODE write, whose pins keep the modes of `modes` for good, says `provides.pins.fixed_modes: true` (the PCA9685): its pins are ready for each of those modes with no start command (the DAC53202 too). A device whose pins (channels) depend on a config field says it in `provides.pins.by_config` (`{field, variants: [{value, count, labels}]}`; `count` and `labels` beside it are the variant of the field's default): the DRV8962's channels are two full bridges (`Bridge A`, `Bridge B`) or four half bridges (`OUT1`..`OUT4`) by its `topology`, and the app offers a device's channels by its config (`devicePins`). A device whose config settings can change after CREATE gives an own command with both sides, a read that takes nothing and a write whose fields are those config fields, by the same ids and types (the DRV8962's `settings`: topology, current limit, braking): the app edits those fields live in the device's Settings and keeps them in the start commands of a board device. A command that takes a pin and has no `pin_state` is offered every pin that is not reserved or managed by another device. A new IO-style command needs it in the same change.

## The `runit-app/generated/` bundle (what the app reads)

`generate_bundle.py` copies the descriptors from `runit-esp/components/` into `runit-app/generated/`, so the app reads one place and never the source tree:

| Folder | Content | Taken from |
|---|---|---|
| `runit-app/generated/devices/` | `device_*.json` | `runit-esp/components/devices/*/` |
| `runit-app/generated/contracts/` | `contract_*.json` with `scope` `device` (or none) | `runit-esp/components/sys/*/` and others |
| `runit-app/generated/services/` | `contract_*.json` with `scope` `system` | the same places |
| `runit-app/generated/blocks/` | `block_*.json` | `runit-esp/components/vm/vm_blocks/**/` |
| `runit-app/generated/manuals/<id>/` | `manual.md` and the pictures beside it (`.png`, `.jpg`, `.gif`, `.svg`, `.webp`), keyed by the device's `id` | `runit-esp/components/devices/*/manual/` |
| `runit-app/generated/sources/<id>/` | the device's own `device_*.c` and `device_*.h`, a copy for the app's device page (read only) | `runit-esp/components/devices/*/` |
| `runit-app/generated/common/` | `enums.json` (written by `generate_enums.py`), `limits.json` (written by `generate_limits.py`), `errors.json` (with every module's messages), `board.json`, `board_devkit.json` | generated / `runit-esp/components/core/errors/`, `runit-esp/components/app/runit/` |

A device's manual is written by hand in `runit-esp/components/devices/device_<chip>/manual/manual.md` (English). Another language is `manual.<lang>.md` beside it (`manual.pl.md` is Polish); the app shows the reader's language and falls back to English. Pictures sit in the same folder, named in the markdown as `![alt](name.png)`. The manual describes the chip; a picture is an example board. The manual and the code copy are read by the app's device page; a device without a manual folder shows "No manual yet". A copy is the source with `$schema` rewritten to point at `templates/` from its folder. The script also deletes copies whose source is gone. A new device or contract appears in the bundle by its file name; there is no list to edit. The source stays in `components/`; edit it there, never in `runit-app/generated/`.

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

`generate_limits.py` writes `runit-app/generated/common/limits.json` with the value the firmware is built with (`sdkconfig`, like `generate_enums.py`) and the first paragraph of the `help` as the description. `@alias` is required, `@unit` optional; nothing unmarked is published (task priorities and stack sizes are not limits a program can hit). Change the value (`idf.py menuconfig`, then `idf.py reconfigure`) and run `sh templates/sync.sh`. The app reads the file; what it derives (the payload of a frame is `CMD_FRAME_MAX`, group `cmd_frame` of `enums.json`, less `[seq][flags]` and `[key][dir][op]` of a VM command) is its own arithmetic, checked against the C by its tests.

## Core versus scanned

The interpretation of a contract (what an operation does) lives in code and is core. Everything the app shows, validates or encodes comes from these descriptors, so a new device, contract, enum member or error needs a descriptor, not a code change in the app.

A command (`id`, `op`, `symbol`, `label`, `description`, `record`, ...: `contract.schema.json`, `device.schema.json`) has a `read` side, a `write` side or both, each with its own `description`, `args` and `returns`; no `args` or `returns` outside them. The request's direction byte after the root key picks one (`cmd_dir_e`: read 0, write 1); a side the command lacks, or a byte above 1, is refused with `ERR_CMD_DIRECTION`. With both it is a mirror: the read's `args` are the key (a pin, a channel; none for a module's setting) and its `returns` are the write's `args` (`crosscheck.py` holds the two field for field: id, `raw_type` and `size`, in order; the read's `args` are left to the descriptor). A write answers no data unless it gives `returns`. A VM block variant on a command with both sides names the one it runs in `dir` (`vm-block.schema.json`). A variant's `args` and `returns` may give an argument or a returned value the block's own `unit` and `scale` (the wire value is the shown value × scale: volts in the block where the contract has mV, `scale` 1000); the block's C table holds the same number (`scale` for an argument, `rscale` for a result, `vm_block_device.h`), which `crosscheck.py` does not compare.

A service command with both sides whose read takes no key is a board setting: the app's Project screen shows it, read when the board connects, and writes it with its write. A command the app sets by itself carries the tag `"tags": ["app"]` and is no setting (`SYS TIME`: the app gives the board the clock at every connection). The dead-man (`SYS DEADMAN`) is a setting with no `record` on purpose: the board keeps it in RAM only and starts as built (off), so a board that restarts runs its program offline; the app writes it at once (Apply now) and never stores it in the project.

Where the setting is kept: a command with `"record": "SYS_ACTIONS_ID_…"` (a member of `settings_record`, published from `sys_actions.h`) is the **project's**: the app writes the frame of its write as that settings record, a section of the stored project, so it is applied at every start. Without `record` the board keeps the setting itself (`PROJECT OPTIONS`): the app writes it at once. A setting may be a table (`POWER_MANAGER MATRIX`): both sides are rows (`"array": true`), the row's target first and an array field whose element *i* is the member of value *i* of the enum `index_enum` names (`sys_power_kind_e`): the columns. A service whose settings are one record (`"record"` on the service: BLE's GATT table, the data streams) has an editor in the app (`runit-app/src/app/settings/`), which still takes the commands and their fields from the descriptor.

A write changes the pin, channel or device it is given; a read does not. The app offers a pin another device manages to reads only, and its Live power display offers every command whose read answers something, and sends that read every second. A record holds writes only (the board records no read), so the start commands and actions the app builds are writes.

## Synchronization

```sh
sh templates/sync.sh            # regenerate enums.json and errors.json, refresh runit-app/generated/, then validate everything
sh templates/sync.sh --check    # CI: fail when a generated file or a copy in runit-app/generated/ is out of date, then validate
```

Run it after any change to a descriptor, a schema, a marked enum or define group, or an error map; the commit should leave it green. `sync.sh` runs `generate_enums.py`, `generate_limits.py`, `generate_errors.py`, `generate_bundle.py`, `validate_schemas.py`, `crosscheck.py` and `check_library.py`. `check_library.py` holds every part of `library/parts/` to `part.schema.json` and checks its references: the folder is its id and its drawing has a `viewBox` of its size in mm; its device type is a published device descriptor, its preset one of the descriptor's, its `config` keys fields of it; a pin's `config` is a pin field of that device, `device_pin: "self"` needs a device, `net` / `supply` / an element's `in` and `out` name nets of the part; a board part names a board.json; ids are unique. `crosscheck.py` checks what no single schema can: a device's `contracts` name existing contracts; a command with both sides is a mirror (above); a VM block's `wire_id` is unique, below `VM_BLOCK_TYPES_MAX` and equals `VM_BLK_<ID>` of `runit-esp/components/vm/vm_core/block/vm_block_ids.h`, and its `state_len` equals the `.state_len` of its `VM_BLOCK_TYPE_DEFINE` (a `sizeof` that a `_Static_assert` pins to a number; 0 when the definition names none), and the `idle` options of a device block are 0 or the op code of a command with a write side (`(contract_id << 8) | op`, or `0xFF00 | op` for a device's own command); `device.contract` / `device.device_type` exist; a block's `opcodes.enum` (the opcode group of an expression block, with the type of its constants) is published in `enums.json` and the block has an `editor`; a variant's `op` is a command of that contract, its `dir` one of that command's sides (required when it has both) and its `args` keys are arguments of that side; `fixed` / `default` / condition values are members of the enum group, options or contract choice they refer to; `{name}` in a header or body is a field, argument, pin, `device` or `variant`; a condition names a field of the block, `device.<config id>` of the block's `device_type` (a contract block has no device config to name) or a variant. Block types without a descriptor are listed, not failed. The validator checks every descriptor (devices, contracts, VM blocks, errors and the `errors_<module>.json` texts, board, enums, limits) against its schema (JSON Schema 2020-12) and that each enum a descriptor names (`"enum"` of a field, `enum_ref` of an error field) is published in `enums.json`. `sync.sh` uses the Python of the repository's `.venv` (agents run it that way too), falling back to `python`; `PYTHON=<interpreter>` overrides it. The validator needs `jsonschema` in that Python (`.venv/Scripts/python.exe -m pip install jsonschema`).
