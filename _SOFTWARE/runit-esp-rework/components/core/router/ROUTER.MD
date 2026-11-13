# router — how a command is handled (for agents)

Component `components/core/router/`. The smallest layer for commands: the **handler** (`cmd_f`), the **direction** (read or write), the **answer**, the **frame size** every limit comes from, the **op tables** and the **tree of routers** (a root table of keys, then each module's own router), and the errors a handler raises for a bad payload or a missing side. No queue, no task, no transport: a device registry, a contract, a driver and a module router build on it alone, so a device compiles without the queue (`router_queue`, ROUTER_QUEUE.MD).

```
router_queue   (queue, two tasks, origin, answers to callbacks)   ─┐ routes with
router         (root table of keys, module tables: [key][dir][code][args])  ◄─┘
sys_device     (op tables per device type: [id][op][args])  ───────┐ the handler type, the tables
                                                                   └──────► router   (cmd_f, cmd_side, cmd_reply, ERR_CMD_*)
```

The device registry and the queue both depend on `router`, **not on each other**:
- A device driver or a contract compiles **without the queue**.
- An error for a bad payload in a device op is `ERR_CMD_BAD_LEN`, not a queue error.
- Code that handles commands can be built and tested independently of the transport layer.

Each `sys_*` module with a remote interface has its own `*_router.c` and depends on `router` **privately** (`PRIV_REQUIRES router`); its public header carries no router headers.

## Files

| File | Role |
|---|---|
| `router.h` | The frame size (`CMD_FRAME_MAX`, `CMD_HEAD`, `CMD_DATA_MAX`), the handler (`cmd_in_t`, `cmd_out_t`, `cmd_f`, `CMD_F_PARAMS`), `cmd_dir_e`, `cmd_side`, `cmd_reply`, `cmd_append`, `router_op_t`, `router_call`, `router_call_op`, `ROUTER_MODULE`, `router_register`, `ROUTER_MODULE_REGISTER`, `router_register_pending`, `router_route` |
| `cmd_table.h` | A table of ops, the one format for every set of commands (a device contract and a module's router): `X(P, NAME, n, key_t, value_t, flags)` rows, `CMD_TABLE_DECLARE`, `CMD_REPLY`, the handlers `CMD_HANDLER` (a fixed frame) and `CMD_HANDLER_TAIL` (a head plus a tail) |
| `router_table.h` | A module's router written from its op table, the one way to write a module's commands: `ROUTER_READ` / `ROUTER_WRITE` / `ROUTER_WRITE_TAIL`, `ROUTER_GETTER` / `ROUTER_SETTER`, `ROUTER_ENTRY*` |
| `errors/router_errors.h`, `errors/errors_router.json` | `ERR_ROUTER_MAP` (`ERR_MOD_ROUTER`, the route and the handler) and the messages |
| `router.c` | The error module and its number, `cmd_side` / `cmd_reply` / `cmd_append`, the root table, `router_route`, `router_call` / `router_call_op`, the routers that registered themselves |
| `Kconfig` | The size of the root table |
| `host/` | Host tests (`test_cmd.c`, `test_router.c`, `host_test.json`; see Tests); not part of the firmware build |
| `AGENTS.md` | Rules for changing this folder |

## The handler

```c
typedef err_h (*cmd_f)(void* ctx, cmd_in_t in, cmd_out_t* out);
```

Bytes in, bytes out, the error chain. `CMD_F_PARAMS` is the parameter list, for the macros that write a `cmd_f`.

- `ctx`: NULL from the router root, or the device's struct from the device registry.
- `in` (`{data, len}`): the payload, the op's args only. A module's table (`router_call_op`) strips `[dir][code]`, the device registry the op; the handler macros (`CMD_HANDLER`, `CMD_HANDLER_TAIL`) parse it. A module router (an entry of the root table) gets its whole command, its key first.
- `out` (`{data, cap, len}`): the answer. `cap` is what the answer may hold (from the router: `CMD_DATA_MAX`), `len` what the handler wrote (0: an empty answer).
- `cmd_reply(out, data, len)` writes the whole answer, `cmd_append(out, data, len)` a piece after what is there; both `ERR_CMD_OUT_FULL` past `cap`.

A command has one answer, at most `cap`. Data longer than that is paged by the command itself, which takes in its request where to start (a `start` id, an `offset`) and answers one page: actions LIST from a `start` ID, actions READ from an `offset`.

## Frame size

`CMD_FRAME_MAX` (512) is the largest whole frame on every provider, and `CMD_HEAD` (5) the room a frame keeps for its header in front of its data: the session's header (SYS_SESSION.MD "Frames"). They are published to the app (group `cmd_frame` in `runit-app/generated/common/enums.json`): the same number on the board and the client, so nothing is sized per link.

```
CMD_FRAME_MAX
 ├─ a framed provider: a frame it takes or sends whole; its driver's buffers hold one
 │    (BLE: a framed characteristic's item, sys_ble; UART: the frame, its CRC and COBS, sys_uart)
 ├─ CMD_DATA_MAX = CMD_FRAME_MAX − CMD_HEAD: a stored frame (an action record's, a project section's),
 │    the answer buffer of a replay
 └─ a stream's frame to a subscriber [stream id][payload]: a payload ≤ CMD_DATA_MAX − 1
```

Every frame has the session's header, exactly `CMD_HEAD` bytes (`[frame byte][dst][src][seq u16]`). So a request (`[key][dir]…`) and an answer's data are at most `CMD_DATA_MAX`, and the router keeps `CMD_HEAD` bytes in front of an answer for its frame's header (ROUTER.MD "Commands and answers"). A raw provider's link carries less, and its driver refuses what does not fit (SYS_DATA_PROVIDER.MD "Frame size"). No size other than these is known above a driver.

## Long work

A handler waits on nothing but its own I/O (a flash write, an I2C transfer): it runs on a router task, and a device op under the registry's lock (SYS_DEVICE.MD "Concurrency"). What takes long or waits on something outside is a **job**: its write starts it and answers at once; its progress and its end come on a stream, or by a read.

## Read and write

Every request names the side of its command it runs: the `cmd_dir_e` byte right after the root key (ROUTER.MD "A command"), `CMD_DIR_READ` 0 or `CMD_DIR_WRITE` 1. A command has a read side, a write side or both, each its own `cmd_f` (`router_op_t` in the router, the op tables of `sys_device`). `cmd_side(read, write, dir, &fn)` picks one: `ERR_CMD_DIRECTION{dir}` for a side the command doesn't have or a dir that is neither.

- A read changes nothing; a write sets a value or runs an action.
- With both sides the command is a **mirror**: the read takes the key (a pin, a channel; nothing for a module's setting) and answers what the write takes. A write answers no data unless its command declares an answer. The descriptors hold the same rule: templates/AGENTS.md (`crosscheck.py` checks it).

## Routing

- A module router is a `cmd_f` (ROUTER.MD): `in` starts at **this router's key**. The root calls `router_call(table, n, ctx, in, …)`, which looks up the key, calls that entry with the same `ctx`, and wraps a failure in `ERR_ROUTER_KEY{key}`. A module router consumes its key and calls `router_call_op(k_ops, n, ctx, {in.data + 1, in.len - 1}, out)` on `[dir][code][args]`: it finds the code's row, takes the side `dir` asks for (`cmd_side`), calls it with the args and wraps a failure in `ERR_ROUTER_OP{code}`. So every level shows in the error chain, the root's as KEY and the module's as OP (a code means something only within its module: the app names it by the KEY before it).
- **The op table**: a module's commands are one table `P_OPERATIONS(X, P)` of rows `X(P, NAME, code, key_t, value_t, 0)` in its `*_router.h`, the format of a device contract (`cmd_table.h`), and `CMD_TABLE_DECLARE(P)`: `P_OP_<NAME>` (the code), `P_key_<NAME>_t`, `P_value_<NAME>_t`. The read takes the key and answers the value, the write takes the value (ROUTER.MD "Read and write"). A router's flags are 0.
- **Writing ops** (`router_table.h`): the native C API stays the source (typed, no packing on internal calls, callbacks and handles); an op only adapts it.
  - `ROUTER_GETTER(P, NAME, get)` / `ROUTER_SETTER(P, NAME, set)`: the side as the module's getter `err_h get(P_value_NAME_t*)` or setter `err_h set(const P_value_NAME_t*)`, in one line.
  - `ROUTER_READ(P, NAME) { … }` / `ROUTER_WRITE(P, NAME) { … }`: a side with logic of its own; the body sees `args` (the key or the value, a copy) and `out`, and answers with `CMD_REPLY(P, NAME, fields)`, `cmd_reply` or `cmd_append`.
  - `ROUTER_WRITE_TAIL(P, NAME) { … }`: a write whose payload is its value (the head, packed) and then bytes of any number (a name, data); the body sees `args` (the head, in place), `tail` and `tail_len`, and checks what the tail must be.
  - The rows: `ROUTER_ENTRY(P, NAME)` (both sides), `ROUTER_ENTRY_R`, `ROUTER_ENTRY_W`, in a `router_op_t k_ops[]`; `ROUTER_MODULE(fn, k_ops)` writes the module's `cmd_f` over it (`router_call_op`; prefix `static` for a file-local one), `ROUTER_MODULE_DECLARE(fn);` declares it in the module's `*_router.h`.
  - The length check (`ERR_CMD_BAD_LEN{got, need}`, the payload's bytes) is done before the body runs. A frame of no fixed shape (the power manager's MATRIX: rows, as many as the board has) is a hand-written `cmd_f` named `P_w_NAME` / `P_r_NAME`, so its row is still `ROUTER_ENTRY*`. Remote-only policy belongs in the body, not in the native API.
- Root: a key is bound to a module router in one of two ways, both before the router tasks start. The app layer calls `router_register(key, fn)` at boot, before `router_init` (router_queue.h; the module exports its `cmd_f`). Or the module registers itself with `ROUTER_MODULE_REGISTER(NAME, fn)` (the key is the module's number, `MODULE_ID_<NAME>`, `module_id.h`) in its `.c`: a constructor keeps the pair (`router_register_later`, no check and no error made before `app_main`), and `router_register_pending` (called by `router_init`) registers it and returns `ERR_ROUTER_KEY_TAKEN` / `ERR_ROUTER_TABLE_FULL` like `router_register`. Two modules cannot have one number (`module_id.h`); nothing references such a module, so its component is linked `WHOLE_ARCHIVE`. A module's router header is also the list of its ops: the frames, then the op table; the module's own header only forward-declares a frame its API needs.
- `out->cap` is `CMD_DATA_MAX` for every command; answers use `cmd_reply` (`ERR_CMD_OUT_FULL` when too big). A command answers once: data longer than that is paged by the command (ROUTER.MD "The handler").
- `router_route(in, out)` is also the entry point for replaying stored frames (actions, project), on the calling task.
- **Concurrency**: urgent and normal commands run concurrently on two tasks, so module routers must be thread-safe.

## Root keys

A root key is the number of the module that owns the router (`MODULE_ID_<NAME>`, `core/utils/module_id.h`): the same number is the module's error module and the module of its events. There is no list of keys; a module's doc names its key and its commands (SYS_DEVICE.MD, SYS_POWER_MANAGER.MD "Remote"). `ROUTER_MODULE_REGISTER` binds the key to the router; `0` is invalid.

## Errors (`ERR_MOD_ROUTER`, `MODULE_ID_ROUTER` = 0x23)

A tag about a command carries its `src`.

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_ROUTER_KEY` | NONE | `u8 key` | Route step at the root (wraps the module's error) |
| `ERR_ROUTER_OP` | NONE | `u8 code` | Route step in a module (wraps the command's error) |
| `ERR_ROUTER_NO_KEY` | HIGH | – | The command ended where its key or its op code was expected |
| `ERR_ROUTER_UNKNOWN_KEY` | HIGH | `u8 key` | No module under the root key |
| `ERR_ROUTER_UNKNOWN_OP` | HIGH | `u8 code` | No command of the module has the code |
| `ERR_ROUTER_KEY_TAKEN` | HIGH | `u8 key` | Root key already registered |
| `ERR_ROUTER_TABLE_FULL` | HIGH | `u8 key` | Root table capacity (`CONFIG_ROUTER_MAX_KEYS`) reached |
| `ERR_CMD_BAD_LEN` | HIGH | `u16 got, u16 need` | Payload size does not match expected arguments size |
| `ERR_CMD_OUT_FULL` | HIGH | `u16 need, u16 cap` | Response exceeds output buffer capacity |
| `ERR_CMD_BAD_ARG` | HIGH | – | Invalid argument value (out of range, invalid format) |
| `ERR_CMD_DIRECTION` | HIGH | `u8 dir` | The command has no such side, or dir is neither read nor write |

The queue's errors (`ERR_ROUTER_QUEUE_FULL`, `_FRAME_LEN`, `_BUSY`) are `ERR_MOD_ROUTER_QUEUE` (ROUTER_QUEUE.MD).

## Limits

- `CMD_FRAME_MAX` (512 bytes, `router.h`): the largest frame; `CMD_DATA_MAX` the largest request and answer. The router has no frame limit of its own.
- `CONFIG_ROUTER_MAX_KEYS` (default 24): maximum registered root keys.

## Tests

Host tests in `host/` (`python test_host/run.py router` from the firmware project, `HOST_TESTS.MD`): the real `router.c` with the real `errors`, on the PC. Two executables:
- `test_cmd.c`: `cmd_side` (the handler of each direction, a missing side, a dir that is neither read nor write), a command run through its side, `cmd_reply` (the whole answer, exactly `cap`, replacing an earlier answer, too long refused with nothing written, an empty answer), `cmd_append` (an answer in pieces, exactly `cap`, past `cap` refused with the earlier pieces kept, `need` the size the whole answer would be) and the error tags' IDs and payload sizes (wire format).
- `test_router.c`, the routing, on a test module written from its table (`TEST_OPERATIONS`): the handlers (`CMD_HANDLER`: exactly the args, one byte short or long, no payload, a byte after an op without args; `ROUTER_WRITE_TAIL`: the head and the tail, an empty tail, no head), `router_call_op` (the side a direction asks for, `[dir][code]` too short, unknown code, a side the command lacks wrapped in `ERR_ROUTER_OP`, a bad payload), `router_call` (key, ctx and the whole input passed, unknown key, `ERR_ROUTER_KEY` wrapping), the whole route root key to module to op (the chain KEY, OP, cause; the answer empty after a failure), `router_register` (key 0, no handler, taken, full table keeping what is bound).
