# router

Spec: `ROUTER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Decoupling invariant: `router` must not depend on `router_queue` or `sys_device`. It builds on `cmd` (`components/core/cmd/`), `errors` and `utils`. Non-routing consumers (`sys_device`, device drivers, contracts) depend on `cmd`, keeping `router` (`router.h` / `router_table.h`, `router_*` / `ROUTER_*`) isolated to module routers and dispatch.
- `cmd_f`, `cmd_dir_e`, `CMD_FRAME_MAX`, `CMD_HEAD`, the header (`cmd_hdr_t`, `CMD_TYPE_*`, `cmd_kind_e`, `CMD_SRC_*`), `cmd_reply`/`cmd_append`, `cmd_table.h`, and `ERR_CMD_*` (`MODULE_ID_CMD` = `0x27`) live in `components/core/cmd/` (`CMD.MD`).
- Error tags in `ERR_ROUTER_MAP` (`MODULE_ID_ROUTER` = `0x23`): the route (`ERR_ROUTER_*`). The app and the scripts know them by name; `ERR_ROUTER_KEY` stays code 1 (an app test frames it by hand). Changing them requires `sh templates/sync.sh`.
- Root keys are the modules' numbers (`MODULE_ID_DECLARE` in each module's `<module>_id.h`, `module_id.h`); there is no list of them.
- Root registration: modules register exclusively via `ROUTER_MODULE_REGISTER(MODULE_ID_<NAME>, fn)` (the key is the module's number, `MODULE_ID_<NAME>`) in the module's `.c`: its constructor only keeps the pair (`router_register`), and `router_root_init` binds it into the root table and returns the error. A self-registering module's component is `WHOLE_ARCHIVE`. Nothing in a constructor builds an error.
- Command tables and handlers:
  - A module's commands are one op table `P_OPERATIONS(X, P)` (`cmd_table.h`, the format of the device contracts) in its `*_router.h`. Its sides are written with `router_table.h` (`ROUTER_GETTER` / `ROUTER_SETTER`, `ROUTER_READ` / `ROUTER_WRITE` / `ROUTER_WRITE_TAIL`) and its rows with `ROUTER_ENTRY*`; `ROUTER_MODULE` runs them (`router_call_op`: the side `dir` asks for, a failure wrapped in `ERR_ROUTER_OP{code}`, an unknown code `ERR_ROUTER_UNKNOWN_OP`). Never a `#define` of an op code, a hand-written row, or a hand-written `cmd_f` for a frame of a fixed shape. A command with both sides follows the mirror (ROUTER.MD "Read and write"): the read answers its value_t.
  - A handler's `in` is the payload only, for a module's op and a device's alike; the size checks are `CMD_HANDLER` (exact) and `CMD_HANDLER_TAIL` (at least the head), the one place a payload is parsed.
  - Module routers must be thread-safe when accessing shared resources.
- Linker requirements: `CMakeLists.txt` must retain `WHOLE_ARCHIVE` (the error tags are registered by a constructor in `router.c`).
- Host tests: `host/` (ROUTER.MD "Tests"). A change to the tables or routing is run with `python test_host/run.py router` (`python test_host/run.py cmd` for `cmd`). A new key a test registers is never removed: `test_router.c` registers its own and fills the table last.
