# Contract annotations

`generate-contracts.py` publishes **every command the board answers** as `data-structures/contracts/contracts.generated.json`: the table the app's codec is built from. The C that implements a command is the only source; every name a user sees is an explicit `@alias` (a field, op or event without one is an error, so the app never labels anything from a C identifier). Generated output is not edited.

```powershell
python data-structures/auto-annotations/contracts/generate-contracts.py [--check]
python -m unittest data-structures/auto-annotations/contracts/test_generate_contracts.py
```

A command is `[key][op][payload]` (the router, ROUTER.MD), a device command `[0x05][device u16][op u16][args]`. The catalog has two parts:

| Part | What it lists | Where it is written |
|---|---|---|
| `routers[]` | every `ROUTER_KEY_*` (alias and description from the `router_key` enum, `enums.json`) and its ops | the op table of the module that `router_register(KEY, module)` binds |
| `device_contracts[]` | every device contract: ops, events, ids | the contract's X-macro tables |

The same struct grammar describes an argument, an answer and a stream frame, so one decoder reads both directions (the VM telemetry frame is a `SET_DATA` command).

## Wire structs

Packed little-endian structs (`typedef struct __attribute__((packed)) {...} name;`), laid out sequentially; a nested struct is expanded in place; a field type is a fixed-width integer, `float` or another struct. A size `_Static_assert(sizeof(T) == N)` next to it is checked. An enum field is a `uint8_t` / `uint16_t` with `@enum-ref <enum>`. The tags stand in the trailing comment of the field (a comment-only line under it continues it):

| Tag | Meaning |
|---|---|
| `@alias <name>` | required: what the field is called (padding fields start with `_` and need none) |
| `@description <text>` | for a tooltip |
| `@unit`, `@min`, `@max`, `@default` | the field's unit and limits |
| `@enum-ref <enum>` | the value is a member of a published `//#ref-enum` (enums.json) |
| `@choices <v>=<Name>,...` | a few literal values (`_` is a space inside a name): `0=Low,1=High` |
| `@id device\|pin\|channel\|event\|action` | the field holds an id: a device (u16), an index of a pin or channel of a device, an event, a record of the action store |
| `@device-field <field>`, `@any <n>` | the id belongs to the device in that field; `<n>` is stored for "any" |

## Router ops

In the module's `.c`, above each row of `static const router_entry_t k_ops[] = {...};` (one row per line):

```c
//@op SYS_ACTIONS_OP_LIST @alias List records @description The stored record ids from a given one up @returns u16 @returns-alias Stored records @returns-tail u32[] @returns-tail-alias Record ids
{SYS_ACTIONS_OP_LIST, op_list},
```

- `//@op <SYMBOL>` must name the row below; `@alias` and `@description` are required. The op id is the `#define <SYMBOL> <n>` (in the module's files or any header).
- **Arguments** are the struct of the handler macro (`ROUTER_OP(op_x, args_t)`; `ROUTER_OP_TAIL(op_x, hdr_t)` adds a tail; `ROUTER_OP_NOARGS`). A handler written by hand (`VM_OP_FN`, a plain `cmd_f`) says it with `@args <struct>`; a payload that is not one struct (a batch of records) with `@payload <text>`. `@args` must agree with the macro.
- **Tail**: a handler with a tail needs `@tail <type> @tail-alias <name> [@tail-description ..]`.
- **Answer**: `@returns <type>` (nothing: the op answers no data) with `@returns-alias <name>` for a scalar and optional `@unit`, `@choices`; a struct name publishes its fields (annotated as above). A sequence after it: `@returns-tail <type>[] @returns-tail-alias <name>`. Types: `u8 u16 u32 u64 i8 i16 i32 i64 f32`, `string` (NUL-terminated), `bytes`, `<struct>`, and `T[]` of any of them.
- `@urgent`: the app should send it urgent (STOP): the firmware has no list, the app decides, this is the author's hint.
- A router with no table says how it dispatches in its `.c`: `//@dispatch <kind> <layout>` (the device router: `[device id u16][op u16][args]`).

## Device contracts

An X-macro table that generates the op ids (`enum { SYS_IO_CONTRACT(SYS_IO__ID) };` with `#define SYS_IO__ID(name, n, args_t) IO_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_IO, n)`) is a contract, found by that pattern. One row per line, the comment is the annotation:

```c
#define SYS_IO_CONTRACT(X) \
  X(SET_LEVEL, 0x00, io_level_args_t)  /* @alias Set level @description Drives a pin high or low. */ \
  X(GET_LEVEL, 0x02, io_pin_args_t)    /* @alias Read level @description Reads the level of a pin. @returns u8 @returns-alias Level @choices 0=Low,1=High */
```

The third column is the args struct (`sys_device_none_t`: none); `@returns` / `@returns-tail` are as above. An events table (`#define SYS_IO__EV(name, n, flags) IO_EV_##name = ...`) takes `/* @alias <name> @description <text> [@value <what the value is>] [@unit <u>] */` per row; `SYS_EVENT_NOTIFY` in the flags is `notify: true`. A contract is published only with an id in the `device_contract` enum (`sys_device.h`, aliases and descriptions there). The op id is `(contract << 8) | n`; CREATE (`[type u16][the type's create struct]`) is the one op with no row and is added by the generator.

A device's **own** ops (`SYS_DEVICE_OWN_OP(n)`, the `0xFFxx` range) are not here: each device descriptor lists them (device-annotations.md).

## Output

`frame` (the request and response layout, the status enum, `byte_order`), `id_kinds`, `routers[]` (`key`, `symbol`, `alias`, `description`, `module`, `source_file`, `ops[]` with `id`, `symbol`, `alias`, `description`, `args {struct, size, fields[]}`, `tail`, `returns`, `returns_tail`, `payload`, `urgent`; or `dispatch`) and `device_contracts[]` (`name`, `id`, `alias`, `ops[]` with `op` and `id`, `events[]`). A field carries `name`, `offset`, `size`, `type` (or `struct` + `fields`, `array_len`), and its tags (`alias`, `unit`, `min`, `max`, `enum_ref`, `choices`, `id_kind`, `device_field`, `any_value`).
