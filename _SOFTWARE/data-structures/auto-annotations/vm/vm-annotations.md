# VM auto-annotations

`//#vm-struct-ref` publishes a VM C struct to `data-structures/vm/vm-model.generated.json`. The C declaration is the source of truth; generated JSON is not edited.

```c
//#vm-struct-ref @alias VM Object Header @kind vm-object-header @wire-abi esp32-gcc-bitfield-v1 @wire-size 4
typedef struct __attribute__((aligned(4))) vm_obj_head_t {
  uint16_t payload_size;  //@alias Payload Size @unit bytes @role payload-size @wire-offset 0 @wire-type u16-le
  ...
    uint8_t upd : 1;  //@alias Updated @role updated @wire-offset 3 @wire-bit-offset 1 @device-sets 0 at load
```

- Field C types, flexible arrays, nested `struct`/`union` fields, and bit widths are extracted from C.
- `@wire-abi`, `@wire-size`, `@wire-offset`, `@wire-type`, and `@wire-bit-offset` describe the fixed target ABI that TypeScript must mirror.
- `@enum-ref <enum>` identifies the field's exported enum. Pair it with
  `@one-of [$SYMBOL, ...]` to declare its permitted values; `$SYMBOL` values
  are resolved from the shared `//#ref-enum` catalog. `@case` uses the same
  `$SYMBOL` resolution for a union alternative.
- `@internal` marks fields that TypeScript must not author or send on the wire.
- `@device-sets <text>` marks a field the device overrides whatever the app sends (and when).
- Only structs whose in-memory form is also their wire or authoring form are published here; wire records are in `vm-program.generated.json` (below).
- `@derived-from`, `@length-from`, `@element-type-from`, and `@reference` describe generated/linked values.
- C pointers, resolved payload caches, and allocator metadata are runtime-only unless a dedicated wire annotation explicitly maps them to IDs.

Generate the descriptor from the repository root:

```powershell
python data-structures/auto-annotations/vm/generate-vm-model.py
```

## Block catalog (`//#vm-block`)

A block lives in `components/VM/blocks/<name>/`. Two files describe it for the app, and the app builds the block's palette entry, pins, face, detailed view, inspector and program encoding from those two alone (no block-specific app code):

| File | Written by | Holds |
| :--- | :--- | :--- |
| `<name>.display.json` | the author | what the editor shows: `title`, `category`, `description`, `activation` (text), `header` (the face line), `always_detailed`, `eno`, and a `title` / `description` for each pin under `inputs` / `outputs`, keyed by pin name |
| `<name>.content.json` | **generated from the header** (`kind: vm-block-content`) | what the block is: `id`, `name`, activation kind, pins (`index`, `name`, `value`, `overrides`, `required`, ...), `min_enables`, `rules`, `state` (the payload layout with offsets), `min_custom_len`, `enums`, `encoding` for bytecode blocks |

The header `vm_block_<name>.h` is the single source of truth for `content.json`; `generate-vm-blocks.py` also writes `data-structures/vm/blocks/index.generated.json` (palette index pointing at the two files, plus the shared vocabularies: value kinds, activation kinds, id kinds) and the firmware's `vm_block_ids.generated.h` / `vm_blocks_registry.generated.h`. Nothing generated is edited by hand.

Annotations stand next to the C element they describe:

```c
//#block-enum @alias Loop Condition             // a block's own enum: goes into content.json, not enums.json
typedef enum vm_for_cmp_e {
  VM_FOR_CMP_LT = 0,  //@alias <
  ...
} vm_for_cmp_e;

//@data vm_for_code_t                           // the payload struct, fields described on their own lines
typedef struct vm_for_code_t {
  vm_span_t span;      // @description Block ids [start, end) this loop runs; start = own id + 1
                       // @derived start = this block's ID + 1; ...      (a comment-only line continues the field's comment)
  float k_start;       // @description Start when input 0 is unwired
  uint8_t op;          // @description How the iterator advances @enum-ref vm_for_op_e
  uint8_t rt;          // @runtime
  uint8_t _pad[3];
} vm_for_code_t;

//#block-enum @alias For Inputs                 // the pins: one enum per side, one member per pin
typedef enum vm_in_for_e {
  VM_IN_FOR_START = 0,  //@in @value f32 @overrides k_start
  VM_IN_FOR_END,        //@in @value f32 @overrides k_end
} vm_in_for_e;

//@rule op is a vm_for_op_e value and cmp a vm_for_cmp_e value. @error ERR_VM_BLK_BAD_SHAPE
bool vm_verify_for(vm_block_h b);
//#vm-block VM_BLK_FOR @id 5                    // the id (1 to 255, unique) is the block_type on the wire
//@activation enabled
void vm_blk_for(vm_block_h b);
```

- **Pins are the members of `vm_in_<block>_e` / `vm_out_<block>_e`** (marked `//#block-enum`). A member `VM_IN_<BLOCK>_<PIN>` is input pin `<pin>` at the member's value, and its trailing `//@in <tags>` / `//@out <tags>` describes the pin: `@value <kind>` (required: `bool`, `u8`, `u32`, `i32`, `f32` (any scalar object, converted), `scalar` (its own type matters), `gate` (1 while active, cleared quietly: for enables), `object` (whole tree, same shape), `ptr-cell` (a PTR element); the vocabulary is in the index file), `@required`, `@overrides <field>`, `@id`, `@device-field`, `@hidden-by-default`. The C reads a pin by the member (`VM_BLOCK_GET_PARAM(x, b, VM_IN_FOR_START, ...)`), so the name in the C and the pin cannot drift. A member without its tag, a wrong prefix or a gap in the indexes fails the generator.
- **Any number of pins (`*`)**: the last member of the side, named `VM_IN_<BLOCK>_<PIN>_ANY` with `@repeat`, valued after the numbered pins (EXPR: `VM_IN_EXPR_PIN_ANY = 0`; the limit is `CONFIG_VM_BLOCK_MAX_IN` / `_OUT`). It becomes `index: "*"`.
- **Titles and descriptions of pins are not in the header** (a pin without a title in the display.json, a display pin with no member, or a `@title` in the header is an error). The header also may not hold `//@title`, `//@category`, `//@block-description`, `//@header`, `//@eno`, `//@always-detailed`: they belong in the display.json.
- **Directives in the header** (anywhere in it, next to the element; the file holds one block): `//#vm-block VM_BLK_<NAME> @id <n>`; `//@activation <kind>` (`enabled`: every pass while enabled; `triggered`: when a watched input is fresh; `enable-rising`: once each time the enables turn on); `//@enables required|optional`; `//@data <struct>` (above the struct, or beside `//#vm-block` when the struct is shared, as the expression core's); `//@data-tail <text>` (variable data after the struct); `//@rule <text> @error <ERR_TAG>` (a check the block's verify makes; the tag must exist; the checks every block gets are in the index file); `//@opcodes <enum> @stack-max <C macro>` (a bytecode block: the opcode enum and the macro with the evaluator's stack depth; a `//#vm-opcodes <enum>` table of `[OP] = {pops, pushes, VM_*ARG_NONE|INPUT|CONST}` entries is published and checked for a missing opcode); `//@example <title> @in <values> [@consts <values>] @code <symbols> @result <value>` (assembled into the layout, checked against the table and published as golden `custom_data`).
- **Enums:** `//#block-enum` belongs to one block (it goes into its `content.json`); an enum two blocks use, or the app needs globally, stays `//#ref-enum` (`vm_timer_unit_e` is shared by TIMER and PERIODIC; the opcode enums sit in the shared core). Members take `//@alias` and `//@description` as for any enum.
- **The shape comes from the pins**, and the firmware enforces exactly that at load (`vm_block_verify()`, from the generated table): `@required` marks a pin the block cannot run without (`min_in` / `min_q` reach the highest required pin; a required `_ANY` pin asks for one more than the numbered ones), `max_in` / `max_q` are the numbered pins (or the Kconfig limit with an `_ANY` pin), and `//@enables required` asks for at least one enable source (default `optional`).
- **The body and check are found by name**: the header must declare `void vm_blk_<name>(vm_block_h b);` and may declare `bool vm_verify_<name>(vm_block_h b);` (state-field checks only; the shape is not hand-checked). The table entry also gets `sizeof` of the `@data` struct as the state size.
- **The user guide**: `<name>.md` next to the header (and `images/`) is copied to `app/docs/blocks/<name>.md`, like a device's guide. It is the author's text, not checked against the C.
- **State layout is computed** from the struct (natural C alignment; every state struct pads explicitly) and must equal its `_Static_assert(sizeof(...) == N)`, which is required, and every `_Static_assert(offsetof(...) == N)` the header states. Field comments: `@description <text>` (or plain text before the first tag); `@enum-ref <enum>` names a `//#block-enum` of the block or a published `//#ref-enum`; `@runtime` marks device-owned bytes the app writes as 0; `@derived <rule>` marks bytes the app computes from the program (the FOR span); fields starting with `_` are padding. A field type the generator cannot size (anything but the plain `uint8_t` ... `double`) is declared where it is defined: `//#vm-type <name> @size <n> @align <n>` above its typedef, with a `_Static_assert` of the same, so no block's type is named in the generator.
- Two blocks with the same `@id`, or an id outside 1 to 255, fail the generator. The firmware's palette is generated from the same directives: `components/VM/blocks/vm_block_ids.generated.h` and `vm_blocks_registry.generated.h`. All generated files are committed and checked by `--check`.

### Constants: `@overrides`

`VM_IN_FOR_START ... //@in @value f32 @overrides k_start` says: while this pin is unwired the block uses the state field `k_start`. The generator then publishes the pin as hidden until it is wired (`hidden_by_default`) and marks the field `overridden_by` the pin. The app draws no pin for it, shows the field in the inspector under the pin's title, and once the pin is wired shows what feeds it instead. The field must be a user field the pin's value kind can replace (`f32` ↔ `float`, `u32` ↔ `uint32_t`/`uint16_t`/`uint8_t`, ...), each field has at most one pin, and a required pin cannot have one. A hidden pin must be an `@overrides` pin or the dynamic input of a pin mask (below); otherwise nothing could set it and the generator fails.

### Face and detailed view

The display.json says what the block's face shows; the app fills in the values.

- **Face:** `header` is the line under the title (`Every | {period} {time_base}` → "Every 100 MS": the words, then the value, drawn apart; the `|` is optional). Without a header the face shows the title alone. The app resolves the `{ref}`s from the two files (below); the generator checks every one against the pins and fields.
- **Always detailed:** `always_detailed: true` draws the block in its detailed view on every canvas and hides the view choice; it needs an `@overrides` input or `@opcodes`, else the detailed view is empty. IO_SET_LEVEL uses it so its pin and level are always visible.
- **Detailed view:** built automatically, nothing to write. A block has one when it has an input with a constant (`@overrides`), or a formula (`//@opcodes`): each input that has a constant is drawn with that constant as a chip while it is unwired (`0  Start`, `300  Preset`), and a bytecode block shows its formula (`Result = a + b`). A block with neither is its face.
- A `{ref}` in a header is:
  - an **input pin name**: the wired source's name, else (when the pin has `@overrides`) the constant it overrides, else `dynamic` when the user made it a dynamic input;
  - an **output pin name**: the variable it drives, else its name;
  - a **state field** (a user setting): an enum member's `//@alias` if it has one, else its name (`SET_DOMINANT` → `Set dominant`); a device ID field as the device's name; a pin ID as `#22`;
  - `{title}`: the block title.
- A name that is both a pin and a state field is refused unless the pin `@overrides` that field.
- An enum member is worded with `//@alias` on the member: `VM_FOR_CMP_LT = 0, //@alias <`.

### Block editor metadata

These tags apply to state-field comments and (where listed) pin members. They change editor behaviour; they do not change C layout or firmware pin indices.

- `@id device` marks a device ID; `@contract <packet_*_t>` on that field limits the device picker to installed board/project devices supporting that operation. `@id pin @device-field <state field>` (on a pin or a field) marks an index of an IO resource of the device in that field: a pin, or a channel (the title in the display.json says which). The app excludes board-reserved and device-owned pins. The index file's `id_kinds` describes both.
- `@hidden-by-default` (pins; implied by `@overrides`) hides an optional input until it is wired or chosen. Required inputs cannot use this tag.
- A `uint64_t` mask uses `@hidden-by-default @let-user-select-available <pin field> @dynamic-input <input index>`. Static mode derives one bit from the selected constant pin; Dynamic mode reveals that input and a checklist of available pins. The default pin must stay included. Masks are stored as hexadecimal strings in project settings to preserve all 64 bits.

Generate everything with `python data-structures/auto-annotations/generate-all.py` (`--check` in CI, `--test` for the unit tests). The block generator alone:

```powershell
python data-structures/auto-annotations/vm/generate-vm-blocks.py [--check]
```

Its tests (`python -m unittest data-structures/auto-annotations/vm/test_vm_block_editor.py`) generate the real headers, check the faces, and feed it deliberately wrong headers (a face directive in the header, a typo'd tag, a missing `_Static_assert`, a wrong pin enum value, an unknown `{ref}`) to make sure each is refused.

## Program wire format (`vm-program.generated.json`)

`generate-vm-program.py` publishes how the app builds, sizes and watches a program: the class `0x04` records, the telemetry frames, type widths, limits and arena formulas (schema `vm-program.schema.json`). The records are the packed structs in `components/VM/core/loader/vm_wire.h` that the decoder, the loader and `vm_sub` actually use.

```c
//#vm-packet HEADER_packet_vm_add_objs @title Add objects @batch uint8_t @when stopped @description Create objects ...
//@tail name char[head.d.name_size] @alias Name @encoding ascii @description Not NUL-terminated.
//@rule The ID is below obj_cnt. @error ERR_VM_REG_OOB
typedef struct __packed {
  uint16_t id;                           //@alias Object ID @reference object
  uint8_t  head[VM_OBJ_HEAD_WIRE_SIZE];  //@alias Header @struct-ref vm_obj_head_t
} vm_wire_obj_t;
_Static_assert(sizeof(vm_wire_obj_t) == 2 + VM_OBJ_HEAD_WIRE_SIZE, "0x42 record head");
```

- `//#vm-packet <HEADER_symbol>` starts the directive block of a packet's record struct: `@title`, `@description`, `@when` (`any` / `stopped`) are required; `@batch <count type>` makes the body `count` then that many records (`@batch-max <n|symbol>` caps it). `//#vm-wire-struct` starts the block of a struct that isn't a packet itself (an index step). The block sits directly above the `typedef struct __packed`.
- `//@tail <name> <type>[<length field>]` lines (in wire order) describe variable arrays after the fixed part. The length names a record field, or a path into a `@struct-ref` field (`head.d.name_size`). The type is a C integer type or a `//#vm-wire-union`; `@bytes <field>` names the field holding the tail's byte length. Tags: `@alias`, `@description`, `@encoding`, `@reference`, `@none`.
- `//@rule <text> @error <ERR_TAG>`: what the device checks, and the error it answers with. The tag must exist.
- Fields use the shared packet-field grammar, plus `@description`, `@reference object|accessor|block|block-type` (the field holds such an ID), `@none <value>` (the "nothing" ID) and `@struct-ref <vm-model struct>`. Only fixed-size C integer and `float` types; every struct needs `_Static_assert(sizeof(...) == N)`, and the computed layout must equal it.
- `//#vm-wire-union <name> @tag <field> @enum-ref <enum>` with one `//@case $<MEMBER> <struct>` line per member: a tagged element whose case struct starts with the tag field.
- `//#vm-telemetry-stream <CONFIG stream> @class <CONFIG class>` and `//#vm-telemetry <CONFIG header> @record <struct> @title .. @description ..` (in `vm_sub.c`) publish the device-to-app frames; they reuse the upload records.
- `//#vm-arena <item> @per <HEADER_symbol|word> @size <C expression>` sits on the allocation it describes; `//#vm-arena-align <n>` on the bump allocator. Every `sizeof()` in a formula needs a `_Static_assert`, and every `vm_store_alloc()` call in the VM needs a `//#vm-arena` line within 6 lines above it — the generator fails otherwise.
- `#define NAME value  //@vm-constant @description ..` publishes an ID constant. Limits are every `int` / `hex` option in `components/VM/Kconfig`, valued from `sdkconfig` (reconfigure first).
- Type widths come from `vm_obj_type_sizes[]` in `vm_obj.h` (memory) and `VM_OBJ_PTR_WIRE_SIZE` (a PTR element on the wire).

```powershell
python data-structures/auto-annotations/vm/generate-vm-program.py
```
