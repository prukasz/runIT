# cmd — what a command handler is

Component `components/core/cmd/`. The smallest common layer for command handling: the **handler**, the **direction** (read or write), the **answer**, the **frame size** every limit comes from and the **errors a handler raises for a bad payload or a missing side**. No transport, no keys, no registry.

## Files

| File | Role |
|---|---|
| `cmd.h` | The handler (`cmd_in_t`, `cmd_out_t`, `cmd_f`, `CMD_F_PARAMS`), `cmd_dir_e`, `cmd_side`, `cmd_reply`, `cmd_append`, the frame size (`CMD_FRAME_MAX`, `CMD_HEAD`, `CMD_DATA_MAX`) |
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
typedef err_h (*cmd_f)(void* ctx, cmd_in_t in, cmd_out_t* out);
```

Bytes in, bytes out, the error chain. `CMD_F_PARAMS` is the parameter list, for the macros that write a `cmd_f`.

- `ctx`: NULL from the router root, or the device's struct from the device registry.
- `in` (`{data, len}`): what the caller passes. The router passes the command's code first, `[code][args]` (the `ROUTER_OP*` macros strip it); the device registry passes the args only (the contract handler macros parse `sizeof(args)` bytes).
- `out` (`{data, cap, len}`): the answer. `cap` is what the answer may hold (from the router: what the origin link carries now, less the answer's header; at most `CMD_DATA_MAX`), `len` what the handler wrote (0: an empty answer).
- `cmd_reply(out, data, len)` writes the whole answer, `cmd_append(out, data, len)` a piece after what is there; both `ERR_CMD_OUT_FULL` past `cap`.

A command has one answer, at most `cap`. Data longer than that is paged by the command itself, which takes in its request where to start (a `start` id, an `offset`) and answers one page: actions LIST from a `start` ID, actions READ from an `offset`.

## Frame size

`CMD_FRAME_MAX` (512) is the largest whole frame on every link, and `CMD_HEAD` (5) the room a frame keeps for its header in front of its data. They are published to the app (group `cmd_frame` in `runit-app/generated/common/enums.json`): the same number on the board and the client, so nothing is sized per link.

```
CMD_FRAME_MAX
 ├─ a link: a request frame it takes, an answer or stream frame it sends; its buffers hold one
 │    (BLE: a framed characteristic's item, sys_ble; UART: the frame and its hex line, sys_uart)
 ├─ CMD_DATA_MAX = CMD_FRAME_MAX − CMD_HEAD: a stored frame (an action record's, a project section's),
 │    the answer buffer of a replay
 └─ a connector's frame [connector][payload]: a payload ≤ CMD_FRAME_MAX − 1
```

The headers in use fit `CMD_HEAD`: a request's `[seq][flags]` (2), an answer's `[stream][seq][status]` (3). A link may carry less right now (`max_frame`, a BLE MTU): the router's answers and the connectors' frames are cut to that (ROUTER.MD, SYS_DATA_CONNECTOR.MD).

## Long work

A handler waits on nothing but its own I/O (a flash write, an I2C transfer): it runs on a router task, and a device op under the registry's lock (SYS_DEVICE.MD "Concurrency"). What takes long or waits on something outside is a **job**: its write starts it and answers at once; its progress and its end come on a stream, or by a read.

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
