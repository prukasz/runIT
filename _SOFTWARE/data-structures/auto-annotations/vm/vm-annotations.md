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

## Block catalog

The block descriptors have their own grammar and generator: [vm-block-annotations.md](vm-block-annotations.md) (`generate-vm-blocks.py`, tests `test_vm_blocks.py`). The legacy `//#vm-block` / `display.json` grammar that was described here is gone.

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
