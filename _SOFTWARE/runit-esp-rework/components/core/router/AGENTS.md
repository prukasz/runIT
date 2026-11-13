# router

Spec: `ROUTER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Decoupling invariant: `router` must not depend on `router_queue` or `sys_device`. It depends only on `errors` and `utils`. The device registry and the queue build on it and do not know each other, so a device compiles without the queue.
- `cmd_f` (`ctx`, `cmd_in_t in`, `cmd_out_t* out`) is the one command handler signature, used by the module routers (key tables) and the device registry (op tables). It carries no transport, queue or registry logic.
- `cmd_dir_e` (`CMD_DIR_READ` = 0, `CMD_DIR_WRITE` = 1) is wire format (the byte after the root key): do not renumber it. `cmd_side` is the one place a side is picked (ROUTER.MD "Read and write"). The direction byte after the root key (`[key][dir]`) is wire format: every request has it, PUSH frames don't.
- `CMD_FRAME_MAX` and `CMD_HEAD` are the one frame size (ROUTER.MD "Frame size"): no module keeps a frame size of its own, every buffer and limit derives from them.
- Root keys are the modules' numbers (`MODULE_ID_DECLARE`, `module_id.h`); there is no list of them.
- Error tags in `ERR_ROUTER_MAP` (`ERR_MOD_ROUTER` = `MODULE_ID_ROUTER`): the route (`ERR_ROUTER_*`) and the handler (`ERR_CMD_BAD_LEN`, `_OUT_FULL`, `_BAD_ARG`, `_DIRECTION`). The app and the scripts know them by name; `ERR_ROUTER_KEY` stays code 1 (an app test frames it by hand). Changing them requires `sh templates/sync.sh`.
- The answer: `cmd_reply` writes the whole answer and sets `out->len`; `cmd_append` writes after `out->len`; both `ERR_CMD_OUT_FULL` past `out->cap`. `out->len` 0 is an empty answer. A command answers once; longer data is paged by the command (ROUTER.MD "The handler").
- Root registration: `router_register(key, fn)` at boot before `router_init()` (the application), or `ROUTER_MODULE_REGISTER(NAME, fn)` (the key is the module's number, `MODULE_ID_<NAME>`) in the module's `.c`: its constructor only keeps the pair (`router_register_later`), and `router_register_pending` registers it and returns the error. A self-registering module's component is `WHOLE_ARCHIVE`. Nothing in a constructor builds an error.
- Command tables and handlers:
  - A module's commands are one op table `P_OPERATIONS(X, P)` (`cmd_table.h`, the format of the device contracts) in its `*_router.h`. Its sides are written with `router_table.h` (`ROUTER_GETTER` / `ROUTER_SETTER`, `ROUTER_READ` / `ROUTER_WRITE` / `ROUTER_WRITE_TAIL`) and its rows with `ROUTER_ENTRY*`; `ROUTER_MODULE` runs them (`router_call_op`: the side `dir` asks for, a failure wrapped in `ERR_ROUTER_OP{code}`, an unknown code `ERR_ROUTER_UNKNOWN_OP`). Never a `#define` of an op code, a hand-written row, or a hand-written `cmd_f` for a frame of a fixed shape. A command with both sides follows the mirror (ROUTER.MD "Read and write"): the read answers its value_t.
  - A handler's `in` is the payload only, for a module's op and a device's alike; the size checks are `CMD_HANDLER` (exact) and `CMD_HANDLER_TAIL` (at least the head), the one place a payload is parsed.
  - Module routers must be thread-safe when accessing shared resources.
- Linker requirements: `CMakeLists.txt` must retain `WHOLE_ARCHIVE`. The error tags are registered by a constructor in `router.c`; a handler may raise `ERR_CMD_*` without calling `cmd_reply`, so the linker must never discard `router.c`.
- Host tests: `host/` (ROUTER.MD "Tests"). A change to the handler, the tables or the routing is run with `python test_host/run.py router` from the firmware project; the error tags' IDs and sizes are pinned in `test_cmd.c` (wire format). A new key a test registers is never removed: `test_router.c` registers its own and fills the table last.
