# router

Spec: `ROUTER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Root keys in `router_ids.h` (`ROUTER_KEY_*`), response stream byte `CONFIG_ROUTER_RESPONSE_STREAM` (0x05), response status codes (`ROUTER_STATUS_OK` = 0, `ROUTER_STATUS_ERROR` = 1), and `router_cmd_t` framing are wire format. Do not renumber root keys or alter response framing.
- Error tags in `ERR_ROUTER_MAP` (`ERR_MOD_ROUTER` = 2) are wire format: do not renumber them. Changing them requires `sh templates/sync.sh` (regenerates `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green. Payload errors (`ERR_CMD_BAD_LEN`, `_OUT_FULL`, `_BAD_ARG`) belong to `cmd` (`ERR_MOD_CMD` = 11).
- Modifying `contract_sys.json` (commands: PING, STOP, RESTART, DEADMAN_SET, DEADMAN_GET) requires running `sh templates/sync.sh` to update `runit-app/generated/services/contract_sys.json` and validate schemas. Commit with `--check` green.
- Root registration: `router_register(key, fn)` occurs at boot before `router_init()`. Modules do not register themselves; the application (`main`) registers module routers.
- Op macros:
  - Use `ROUTER_OP`, `ROUTER_OP_NOARGS`, `ROUTER_OP_TAIL`, and `ROUTER_MODULE`.
  - The macros parse payloads and enforce exact size matching via `router_args` / `router_args_tail`, wrapping any handler error in `ERR_ROUTER_KEY{key}`.
  - Handlers write responses using `cmd_reply` or `cmd_append`; leaving `out_len` untouched produces an empty response.
- Concurrency & dispatcher tasks:
  - Normal frames run on the work task (`CONFIG_ROUTER_WORK_TASK_PRIO` = 5); frames flagged with `ROUTER_FLAG_URGENT` run on the urgent task (`CONFIG_ROUTER_URGENT_TASK_PRIO` = 15).
  - Module routers must be thread-safe when accessing shared resources.
  - Safe-state callback (`router_set_safe_state`) executes on the urgent task.
- Internal link & injection:
  - `ROUTER_LINK_INTERNAL` (0) is reserved for the board itself (`router_inject`). Injected frames run on router tasks, do not re-arm the dead-man timer, and discard response payloads.
- Multi-packet answers & taps:
  - `router_answer_part` sends intermediate OK packets with matching `seq` before the final answer return.
  - `router_set_tap` provides global command inspection (`take` / `seen`), used by action recording (`sys_actions`).
  - `router_origin()` returns the command currently executing on the calling router task.
- Dead-man watchdog:
  - Runtime dead-man timer is re-armed on every incoming frame from any link. On timeout or link loss (`router_link_lost` with `on_link_lost`), safe state is entered once.

