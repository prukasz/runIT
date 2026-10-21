# cmd — what a command handler is

Component `components/core/cmd/`. The smallest common layer for command handling: the **handler type**, the **direction** (read or write), the **answer helpers** and the **errors a handler raises for a bad payload or a missing side**. No transport, no keys, no registry.

## Files

| File | Role |
|---|---|
| `cmd.h` | `cmd_f`, `CMD_OUT_PARAMS` / `CMD_F_PARAMS`, `cmd_dir_e`, `cmd_side`, `cmd_reply`, `cmd_append`, `cmd_name_arg` |
| `errors/cmd_errors.h` | `ERR_CMD_MAP`, the module's error tags (included by `cmd.h`) |
| `errors/errors_cmd.json` | The error messages |
| `cmd.c` | Error module (`ERR_MOD_CMD` = 11) and helpers. Linked `WHOLE_ARCHIVE`: constructor registers error tags |
| `CMakeLists.txt` | Component registration (`WHOLE_ARCHIVE`, requires `errors` `utils`) |
| `AGENTS.md` | Rules for changing this folder |

## Architecture

Two layers handle commands and each looks them up its own way:

```
router      (root key table, module command tables: [key][dir][code][args])  ─┐
sys_device  (op tables per device type: [0x05][dir][id u16][op u16][args])    ├─►  cmd   (cmd_f, cmd_side, cmd_reply, ERR_CMD_*)
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

- `in`: input buffer. The router passes the command's code first (the `ROUTER_OP*` macros strip it); the device registry passes the args only (contract handler macros parse `sizeof(args)` bytes).
- `ctx`: context pointer. NULL from the router root, or the device's struct from the device registry.
- `out` / `out_cap` / `out_len`: response buffer. Untouched `out_len` means an empty response.
- `cmd_reply`: writes a single contiguous response and verifies buffer capacity (`ERR_CMD_OUT_FULL`).
- `cmd_append`: appends pieces to `out` advancing `*pos` (`ERR_CMD_OUT_FULL`).
- `cmd_name_arg`: verifies that a trailing name argument is strictly NUL-terminated with no bytes following the NUL (`ERR_CMD_BAD_ARG`).

## Read and write

Every request names the side of its command it runs: the `cmd_dir_e` byte right after the root key (ROUTER.MD "Wire format"), `CMD_DIR_READ` 0 or `CMD_DIR_WRITE` 1. A command has a read side, a write side or both, each its own `cmd_f` (`router_op_t` in the router, the op tables of `sys_device`). `cmd_side(read, write, dir, &fn)` picks one: `ERR_CMD_DIRECTION{dir}` for a side the command doesn't have or a dir that is neither.

- A read changes nothing; a write sets a value or runs an action.
- With both sides the command is a **mirror**: the read takes the key (a pin, a channel; nothing for a module's setting) and answers what the write takes. A write answers no data unless its command declares an answer. The descriptors hold the same rule: templates/AGENTS.md (`crosscheck.py` checks it).

## Errors (`ERR_MOD_CMD` = 11)

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_CMD_BAD_LEN` | HIGH | `u16 got, u16 need` | Payload size does not match expected arguments size |
| `ERR_CMD_OUT_FULL` | HIGH | `u16 need, u16 cap` | Response exceeds output buffer capacity |
| `ERR_CMD_BAD_ARG` | HIGH | – | Invalid argument value (out of range, invalid format) |
| `ERR_CMD_DIRECTION` | HIGH | `u8 dir` | The command has no such side, or dir is neither read nor write |

## What stays in the router

`router_call`, `router_call_op`, `router_op_t` (`ROUTER_R` / `ROUTER_W` / `ROUTER_RW`), `router_args`, `router_args_tail` and the `ROUTER_OP*` macros: they handle **keys** (the payload starts after the key byte) and frame dispatching, so they belong to the router. They use `cmd_f`, `cmd_side`, `cmd_reply` and `ERR_CMD_*` tags.
