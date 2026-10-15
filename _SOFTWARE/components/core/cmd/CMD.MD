# cmd — what a command handler is

Component `components/core/cmd/`. The smallest common layer for command handling: the **handler type**, the **answer helpers** and the **errors a handler raises for a bad payload**. No transport, no keys, no registry.

## Files

| File | Role |
|---|---|
| `cmd.h` | `cmd_f`, `CMD_OUT_PARAMS` / `CMD_F_PARAMS`, `ERR_CMD_MAP`, `cmd_reply`, `cmd_append`, `cmd_name_arg` |
| `cmd.c` | Error module (`ERR_MOD_CMD` = 11) and helpers. Linked `WHOLE_ARCHIVE`: constructor registers error tags |
| `CMakeLists.txt` | Component registration (`WHOLE_ARCHIVE`, requires `errors` `utils`) |
| `AGENTS.md` | Rules for changing this folder |

## Architecture

Two layers handle commands and each looks them up its own way:

```
router      (key tables: link frames, root key, nested module keys)   ─┐
sys_device  (op tables per device type: [id u16][op u16][args])        ├─►  cmd   (cmd_f, cmd_reply, ERR_CMD_*)
```

Both end in the same function signature `cmd_f`. The router and the device registry depend on `cmd`, **not on each other**:

- A device driver or a contract compiles **without the router include path**.
- An error for a bad payload in a device op is `ERR_CMD_BAD_LEN`, not a router error.
- Code that handles commands can be built and tested independently of the transport layer.

The router depends on `cmd`. Each `sys_*` module with a remote interface has its own `*_router.c` and depends on the router **privately** (`PRIV_REQUIRES router`); its public header carries no router headers.

## The handler

```c
typedef err_h (*cmd_f)(void* ctx, const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap, uint32_t* out_len);
```

- `in`: input buffer. The router passes its key first (`router_call` strips it); the device registry passes the args only (contract handler macros parse `sizeof(args)` bytes).
- `ctx`: context pointer. NULL from the router root, or the device's struct from the device registry.
- `out` / `out_cap` / `out_len`: response buffer. Untouched `out_len` means an empty response.
- `cmd_reply`: writes a single contiguous response and verifies buffer capacity (`ERR_CMD_OUT_FULL`).
- `cmd_append`: appends pieces to `out` advancing `*pos` (`ERR_CMD_OUT_FULL`).
- `cmd_name_arg`: verifies that a trailing name argument is strictly NUL-terminated with no bytes following the NUL (`ERR_CMD_BAD_ARG`).

## Errors (`ERR_MOD_CMD` = 11)

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_CMD_BAD_LEN` | HIGH | `u16 got, u16 need` | Payload size does not match expected arguments size |
| `ERR_CMD_OUT_FULL` | HIGH | `u16 need, u16 cap` | Response exceeds output buffer capacity |
| `ERR_CMD_BAD_ARG` | HIGH | – | Invalid argument value (out of range, invalid format) |

## What stays in the router

`router_call`, `router_args`, `router_args_tail` and the `ROUTER_OP*` macros: they handle **keys** (the payload starts after the key byte) and frame dispatching, so they belong to the router. They use `cmd_f`, `cmd_reply` and `ERR_CMD_*` tags.
