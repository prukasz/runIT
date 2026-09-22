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
