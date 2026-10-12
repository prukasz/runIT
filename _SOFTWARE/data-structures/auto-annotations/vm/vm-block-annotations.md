# VM block annotations

`generate-vm-blocks.py` reads the block headers and `.c` files under `components/vm/vm_blocks/` and writes what the app needs to build every block by itself, **from JSON and Markdown only** (no block-specific app code):

```
data-structures/vm/blocks/
  index.generated.json     the palette: every block + the shared vocabularies
  <name>.block.json        what the block is and how it looks (one file per block)
  <name>.md                the user guide (copied from the block's folder)
```

Everything is written in C, next to what it describes. There is **no second hand-written file** (the old `display.json` is gone) and nothing generated is edited by hand. Every name a user sees is an explicit **`@alias`**: the generator refuses a block, pin, setting, enum member or operation without one, so the app never has to turn a C identifier into a label. Anything it does not understand is an error.

```powershell
python data-structures/auto-annotations/vm/generate-vm-blocks.py [--check]
python -m unittest data-structures/auto-annotations/vm/test_vm_blocks.py
```

## A block is a folder

```
components/vm/vm_blocks/<group>/<name>/
  vm_block_<name>.h     the annotated header (the one source of the json)
  vm_block_<name>.c     the body, VM_BLOCK_TYPE_DEFINE (the firmware's shape), a device block's op table
  <name>.md             the user guide, written by hand (required)
```

`<group>` is `core` (logic), `device-contract` (one block per device contract) or `device-custom-block` (the own operations of one device type). The block's number is `VM_BLK_<NAME>` in `vm_core/block/vm_block_ids.h`; the generator fails on a number without a block and on two blocks with one number.

## Tags

A tag is `@name value`; a value runs to the next ` @tag` or the end of the line (so a description cannot contain ` @word`). On a field, pin or member the tags stand in the trailing comment of its line; a comment-only line under a state field continues that field's comment.

### The block directive: `//#block VM_BLK_<NAME>`

One per header, with the `//@` lines right under it (a `//` line without a tag continues the previous one). A `//@` line anywhere else is an error.

| Tag | Required | Meaning |
|---|---|---|
| `@alias <name>` | yes | The block's name in the palette and on its face |
| `@category <word>` | yes | Palette group (`data`, `flow`, `time`, `system`, `device`, ...) |
| `@description <text>` | yes | What the block does, one sentence |
| `@activation enabled\|triggered\|enable-rising` | yes | When it runs (the kinds are in the index) |
| `@when <text>` | yes | The activation in words, for the user |
| `@face <template>` | no | The line under the title; `{ref}` below |
| `@enables required\|optional` | no | Needs at least one enable source (default `optional`); must equal `.min_en` |
| `@loop refuse\|once` | no | How it sits in a loop body (`refuse`: it keeps one state; `once`: it acts every turn) |
| `@data <struct>` | no | The private state struct (`custom_data`) |
| `@data-tail <text>` | no | Variable bytes after the struct (bytecode blocks) |
| `@en-alias`, `@en-description`, `@en-signal [true]`, `@en-connect` | no | The enable connector: its name, tooltip, "EN carries a value", what to tell the user while a required enable is missing |
| `@eno-alias`, `@eno-description` | no | ENO's name and meaning (`Q`, `Tick`, ...) |
| `@rule <text> @error <ERR_TAG>` | many | A check the block's `vm_verify_<name>` makes; the tag must exist in an `ERR_*_MAP` |
| `@opcodes <enum> @stack-max <macro> @const-count <field> @code-length <field>` | no | A bytecode block (below) |
| `@formula <template>` | no | How a bytecode block's formula reads under its pins: `{result} = {expr}` |
| `@example <title> @in <v,..> [@consts <v,..>] @code <opcodes and operands> @result <v>` | many | A worked formula, published as golden `custom_data` and checked against the opcode table |
| `@contract <NAME>` or `@device-type <name>` | device blocks | Which devices the block's device field offers: the ones that answer `SYS_DEVICE_CONTRACT_<NAME>`, or the type (`device_servo`) |

### Pins: `vm_in_<name>_e` / `vm_out_<name>_e`

`VM_IN_<BLOCK>_<PIN> = <index>,  //@alias <name> @description <text> @value <kind> [@required] [@overrides <field>] ...`. A repeating pin is the last member `VM_IN_<BLOCK>_<PIN>_ANY` with `@repeat`, valued after the numbered pins.

| Tag | Meaning |
|---|---|
| `@alias` | required: the pin's name (the pin's name is the member's `<pin>` lower-cased, for `{refs}`) |
| `@description` | what it carries, shown in a tooltip |
| `@value <kind>` | required: `bool`, `u8`, `u32`, `i32`, `f32`, `scalar`, `gate`, `object`, `ptr-cell` (the index lists them) |
| `@required` | the block cannot run without it (feeds `min_in` / `min_q` and `required_in`) |
| `@overrides <field>` | while the pin is unwired the block uses this state field as a constant; the field then needs no `@alias` of its own (the pin's is shown) |
| `@repeat`, `@user-write`, `@hide-when-wired`, `@id`, `@device-field`, `@spec`, `@unit` | as the legacy grammar (editor behaviour) |

**The firmware's shape is not annotated.** `VM_BLOCK_TYPE_DEFINE` in the `.c` holds `min_in`, `max_in`, `min_q`, `max_q`, `required_in`, `state_len`; the generator reads them, publishes them as `shape` and **fails when they disagree with the pins** (a `@required` the table does not have, a pin count that is not `max_in`, a `state_len` that is not the struct's size).

### State struct: `//@data <struct>`

Fields are laid out here with natural alignment and compared with `_Static_assert(sizeof(...))` and every `_Static_assert(offsetof(...))` (a field type the generator cannot size is declared `//#vm-type <name> @size <n> @align <n>`). A field's tags:

| Tag | Meaning |
|---|---|
| `@alias <name>` | required for a setting the user edits (unless a pin `@overrides` it) |
| `@description <text>` | |
| `@runtime` | device-owned bytes; the app writes 0 |
| `@derived <rule>` | bytes the app computes from the program (the FOR span, the bytecode counts) |
| `@enum-ref <enum>` | the setting is one of an enum: a `//#block-enum` of the block or a `//#ref-enum` |
| `@id device\|pin\|channel\|event\|action` | the field holds an id: a device (u16), an index of the device in `@device-field` (a pin or a channel: the alias says which), an event or channel of that device, a record of the action store |
| `@device-field <field>`, `@contract <name>`, `@device-type <name>`, `@source-field`, `@any <n>`, `@min <n>`, `@max <n>`, `@unit <text>` | editor metadata: which devices fit, the wildcard stored for "Any", limits |
| `@hidden-by-default @let-user-select-available <field> @dynamic-input <n>` | a pin mask the pin pickers edit |
| `@role device\|op\|idle-op\|const\|idle-const` | a device block's engine state (below) |

Fields starting with `_` are padding.

### Enums

`//#block-enum @alias <title>` above a `typedef enum` is the block's own; `//#ref-enum @alias <title>` is shared (a timer unit used by TIMER and PERIODIC; the opcode enums). Every member of an enum a setting uses needs `//@alias <name>` and may have `@description`; `*_CNT` sentinels are left out.

### The face template, `{ref}`

`@face Timer | {mode} {pt} {time_base}` is the line under the title: the words, then the value, drawn apart (the `|` is optional). A `{ref}` is, by machine name:

- an **input pin**: the wired source's name, else the constant it `@overrides`, else `dynamic`;
- an **output pin**: the variable it drives, else its name;
- a **setting** (a state field): the member's alias for an enum, the device's name for a device id, `#22` for a pin id;
- `{title}`: the block's alias.

A name that is both a pin and a setting is refused unless the pin `@overrides` it; a `{ref}` that names nothing, or a stray brace, is an error. The json carries the parts (`{"text": ...}` / `{"ref": ..., "kind": ...}`), the app fills the values.

## Bytecode blocks (EXPR, EXPR_BIT)

`@opcodes <enum> ...` publishes the opcode table (`encoding.opcodes`): per opcode its value, `alias` (what the editor calls it: `+`, `sqrt`), stack effect (`pops`, `pushes`) and operand kind (`none`, `input`, `const`), read from the `[VM_*_OP] = {pops, pushes, VM_EXPR_ARG_*}` table in C. Each opcode member also has:

- **`@format <template>`**: how it reads in a formula, with `{a}`, `{b}`, `{c}` for the operands in stack order (`a` deepest) and `{input}` / `{const}` for the operand of `IN` / `K`: `{a} + {b}`, `sqrt({a})`, `{a} ? {b} : {c}`, `{input}`. It may use only the operands the opcode pops and must use all of them; an opcode that leaves one value needs it. The app renders the compiled formula back as text with these (the `@formula` line says where the text goes: `Result = {expr}`).
- **`@stack-op`** instead, for `dup`, `drop`, `swap` (they have no infix form).

`encoding.examples` are the `@example` lines assembled into `custom_data` (state header, constants, code), each checked against the table exactly as the load-time check would; they are golden vectors for the app's compiler tests.

## Device blocks

A block with `@contract` / `@device-type` works on a device and shares one engine (`device-contract/vm_block_device.c`). Its pins come from the **operation table** in its `.c`, not from `vm_in_` / `vm_out_` enums, and its state is the engine's `vm_block_device_data_t`, whose fields carry `@role`:

| Role | Field |
|---|---|
| `device` | `device_id` (`@id device`): which device; `contract` / `device_type` are filled from the block directive |
| `op` | `op`: the operation (one of `ops`); it decides the pins |
| `idle-op` | `idle_op`: sent once when the enable turns off; 0 = none; an op with `idle_allowed` |
| `const` | `k[i]`: the constant of input `i` while it is unwired |
| `idle-const` | `idle_k[i]`: the idle operation's inputs after its address inputs |

The table is `static const vm_dev_op_t k_ops[] = { ... };`. Above each row:

```c
//@op HBRIDGE_OP_SET_DRIVE @alias Drive @description Drives a channel: positive forward, negative reverse. @face {channel} at {drive}
//@op-in channel @alias Channel @id pin
//@op-in drive @alias Drive @unit % @min -100 @max 100
{.op = HBRIDGE_OP_SET_DRIVE, .act = VM_DEV_ACT_CHANGE, .result = VM_DEV_R_NONE, .nfield = 2,
 .ftype = {VM_DEV_F_U8, VM_DEV_F_I16}, .scale = {0, 40.95f}},

//@op HBRIDGE_OP_GET_CURRENT @alias Read current @description Reads the current of a channel. @face Read {channel}
//@op-in channel @alias Channel @id pin
//@op-out current @alias Current @unit mA
VM_DEV_OP(HBRIDGE_OP_GET_CURRENT, VM_DEV_ACT_READ, VM_DEV_R_I32, 1, VM_DEV_F_U8),
```

- `//@op <SYMBOL>`: required, names the row below (`@alias`, `@description`, optional `@face` with the op's own `{pin}` names). One `//@op-in <name>` per input field of the row and one `//@op-out <name>` per value it answers, in order; the counts must match the row. Pin tags: `@alias` (required), `@description`, `@unit`, `@min`, `@max`, `@id pin|channel|event`, `@choices <v>=<Name>,<v>=<Name>` (an input or output that is one of a few values; `_` is a space inside a name).
- From the row the generator takes the op's **kind** (`change`: a setting, sent on a change; `edge`: an action, once per rising enable; `read`: asked every pass), the field types (`u8`, `i16`, `u32`, `i32`) and the `scale` (the block's float times the scale is the wire value: HBRIDGE drive is percent, x40.95 to the contract's 12-bit steps). The op's `value` (the id to put in the `op` field) is resolved from the contract's `X(NAME, n, args)` table, or from `#define NAME SYS_DEVICE_OWN_OP(n)` for an own op.
- Checked: the ops fit `max_in` / `max_q` of `VM_BLOCK_TYPE_DEFINE`; a device block has no required pins; the state struct has a field for every role.

## Output (`<name>.block.json`, schema version 2)

`id`, `name`, `alias`, `category`, `description`, `source_file`, `guide`, `activation {kind, text}`, `min_enables`, `loop_use`, `face`, `en`, `eno`, `device`, `shape`, `inputs` / `outputs {min, max, pins[]}` (each pin with `index` or `"*"`, `name`, `alias`, `value`, `description`, `required`, `overrides`, ...), `rules`, `state {struct, size, align, fields[]}` (each field with `offset`, `size`, `source` = `user` / `runtime` / `derived` / `padding`, `alias`, `enum_ref`, `overridden_by`, `role`, the id metadata), `min_custom_len`, `enums`, `encoding`, and for a device block `ops[]` (each with `symbol`, `value`, `alias`, `kind`, `idle_allowed`, `inputs[]`, `outputs[]`, `face`) and the names of its role fields (`op_field`, `idle_op_field`, `const_field`, `idle_const_field`, `device_field`). A pin or setting that is overridden by a pin has no alias of its own: the app shows the pin's. `index.generated.json` lists the blocks and publishes the vocabularies (`value_kinds`, `activation_kinds`, `id_kinds`, `op_kinds`, `field_roles`, `common_rules`).
