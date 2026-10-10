# cmd — what a command handler is

Component `components/core/cmd/` (2026-10-10). The smallest thing both command layers need: the **handler type**, the **answer helper** and the **errors a handler raises for a bad payload**. No transport, no keys, no registry.

| File | Contents |
|---|---|
| `cmd.h` | `cmd_f`, `CMD_OUT_PARAMS` / `CMD_F_PARAMS`, `ERR_CMD_MAP`, `cmd_reply` |
| `cmd.c` | The error module (`ERR_MOD_CMD` = 11) and `cmd_reply`. Linked as `WHOLE_ARCHIVE`: a handler raises `ERR_CMD_*` without calling `cmd_reply`, and the tag registration must not be dropped |

## Why it exists

Two layers handle commands and each looks them up its own way:

```
router      (key tables: link frames, root key, nested module keys)   ─┐
sys_device  (op tables per device type: [id u16][op u16][args])        ├─►  cmd   (cmd_f, cmd_reply, ERR_CMD_*)
```

Both end in the same function type. Before, that type, `router_reply` and the payload errors lived in the router, so the device registry, the contracts (`sys_io`, `sys_pins`, `sys_power`, `sys_event.h`) and every device had to include the router and its links, dispatcher, safe state and dead-man to write a handler. Now the router and the registry depend on `cmd`, **not on each other**, and:

- a device, a contract or `sys_power` compiles **without the router include path** (checked in `compile_commands.json`);
- an error for a bad payload of a device op is `ERR_CMD_BAD_LEN`, not a router error;
- code that handles commands can be built without the transport layer (the base for host tests, later).

The router still depends on `cmd`; each `sys_*` module with a remote interface still has its own `*_router.c` and depends on the router **privately** (`PRIV_REQUIRES router`): its public header carries no router. `sys_device` and `sys_event` are built that way, so only `main` (which registers the routers) sees both.

## The handler

```c
typedef err_h (*cmd_f)(void* ctx, const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap, uint32_t* out_len);
```

`in` is what the caller defines: the router passes its own key first (`router_call` strips it), the device registry passes the args only (the contract handler macros parse exactly `sizeof(args)` bytes). `ctx` is the router's ctx (NULL from the root) or the device's struct. The answer goes to `out` with `cmd_reply`; untouched means an empty answer.

## Errors (`ERR_MOD_CMD` = 11)

| Tag | Data | Meaning |
|---|---|---|
| `ERR_CMD_BAD_LEN` | `{u16 got, u16 need}` | Payload size (the op macros and `SYS_DEVICE_HANDLER` check it before the body runs) |
| `ERR_CMD_OUT_FULL` | `{u16 need, u16 cap}` | The answer doesn't fit what the link carries now |
| `ERR_CMD_BAD_ARG` | – | An argument value the handler refuses (a level above 1, an unknown mode) |

All HIGH. They were `ERR_ROUTER_BAD_LEN` (4), `ERR_ROUTER_OUT_FULL` (5) and `ERR_ROUTER_BAD_ARG` (11): **the tags moved to another module, so their wire IDs changed** (module 2 → 11; the numbers 4, 5 and 11 stay retired in `ERR_MOD_ROUTER`). A client that maps error tags by ID (the app) must take the new table: `ERR_TAG(11, 1..3)`. A device command too short for `[id][op]` now answers `ERR_CMD_BAD_LEN` instead of `ERR_ROUTER_NO_KEY` (which stays the router's: input ended where a key was expected).

## What stays in the router

`router_call` / `router_args` / `router_args_tail` and the `ROUTER_OP*` macros: they are about **keys** (the payload starts after the key byte), so they belong to the router. They use `cmd_f`, `cmd_reply` and the `ERR_CMD_*` tags.
