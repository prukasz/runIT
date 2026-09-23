# VM auto-annotations

`//#vm-struct-ref` publishes a VM C struct to `data-structures/vm/vm-model.generated.json`. The C declaration is the source of truth; generated JSON is not edited.

```c
//#vm-struct-ref @alias VM Object Index @kind vm-index
typedef struct {
  uint8_t kind;      //@alias Kind @role discriminator @enum-ref vm_index_kind_e @one-of [$VM_IDX_LITERAL, $VM_IDX_REF, $VM_IDX_NAME]
  union {            //@discriminator kind
    uint32_t value;  //@case $VM_IDX_LITERAL
  };
} vm_index_t;
```

- Field C types, flexible arrays, nested `struct`/`union` fields, and bit widths are extracted from C.
- `@wire-abi`, `@wire-size`, `@wire-offset`, `@wire-type`, and `@wire-bit-offset` describe the fixed target ABI that TypeScript must mirror.
- `@enum-ref <enum>` identifies the field's exported enum. Pair it with
  `@one-of [$SYMBOL, ...]` to declare its permitted values; `$SYMBOL` values
  are resolved from the shared `//#ref-enum` catalog. `@case` uses the same
  `$SYMBOL` resolution for a union alternative.
- `@internal` marks fields that TypeScript must not author or send on the wire.
- `@derived-from`, `@length-from`, `@element-type-from`, and `@reference` describe generated/linked values.
- C pointers, resolved payload caches, and allocator metadata are runtime-only unless a dedicated wire annotation explicitly maps them to IDs.

Generate the descriptor from the repository root:

```powershell
python data-structures/auto-annotations/vm/generate-vm-model.py
```

## Block catalog (`//#vm-block`)

Every block header in `components/VM/blocks/` describes its block for the app right above its palette entry macro. `generate-vm-blocks.py` writes them to `data-structures/vm/vm-blocks.generated.json` (schema `vm-blocks.schema.json`).

```c
//#vm-block VM_BLK_TIMER @title Timer @category time @state vm_block_timer_data_t
//@block-description IEC timer: on-delay, off-delay or pulse (plus inverted). Q follows the timer, ENO follows Q.
//@in 0 in @title Input
//@in 1 pt @title Preset @description Overrides pt, in time_base units.
//@out 0 q @title Q
//@out 1 et @title Elapsed
#define VM_BLOCK_TYPE_TIMER \
  {.run = vm_blk_timer, .check = vm_verify_timer, .min_in = 1, .min_q = 0, .required_in = 0x1u, .state_len = ...}
```

- `//#vm-block VM_BLK_<NAME>`: the id comes from `#define VM_BLK_<NAME> n` in `vm_blocks.h`. `@title` and `@category` are required; `@state <struct>` names the private-state struct; `@state-tail <text>` describes variable data after it (expression literals and bytecode).
- `//@block-description`, then one `//@in` / `//@out` line per pin: `<index|*> <name> @title <text> [@description <text>]`. `*` means "any number" (expression inputs, switch branches).
- **Shape is not annotated**: `min_inputs`, `min_outputs` and each input's `required` flag are read from the `VM_BLOCK_TYPE_<NAME>` macro the firmware checks at load. Every required input must have an `//@in` line.
- **State layout is computed** from the struct (natural C alignment; every state struct pads explicitly) and must equal its `_Static_assert(sizeof(...) == N)`. Field comments: text before the first tag is the description; `@enum-ref <enum>` names a published `//#ref-enum`; `@runtime` marks device-owned bytes the app writes as 0; fields starting with `_` are padding. A new field type needs a size in the generator's `TYPES` table.
- Every palette id (except 0) must have a `//#vm-block`; the generator fails otherwise.

```powershell
python data-structures/auto-annotations/vm/generate-vm-blocks.py
```
