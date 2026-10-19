# router

Spec: `ROUTER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Root keys in `router_ids.h` (`ROUTER_KEY_*`), response stream byte `CONFIG_ROUTER_RESPONSE_STREAM` (0x05), response status codes (`ROUTER_STATUS_OK` = 0, `ROUTER_STATUS_ERROR` = 1), and the error record (`router_err_record`, ROUTER.MD "Error record") are wire format. Do not renumber root keys or alter response framing; a change of the record is mirrored in the app's decoder (`runit-app/src/backend/protocol/errors.ts`) and `router_link.py`.
- Error tags in `ERR_ROUTER_MAP` (`ERR_MOD_ROUTER` = 2) are wire format: do not renumber them. Changing them requires `sh templates/sync.sh` (regenerates `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green. Payload errors (`ERR_CMD_BAD_LEN`, `_OUT_FULL`, `_BAD_ARG`) belong to `cmd` (`ERR_MOD_CMD` = 11).
- Root registration: `router_register(key, fn)` occurs at boot before `router_init()`. Modules do not register themselves; the application (`main`) registers module routers.
- Op macros:
  - Use `ROUTER_OP`, `ROUTER_OP_NOARGS`, `ROUTER_OP_TAIL`, and `ROUTER_MODULE`.
  - The macros parse payloads and enforce exact size matching via `router_args` / `router_args_tail`, wrapping any handler error in `ERR_ROUTER_KEY{key}`.
  - Handlers write responses using `cmd_reply` or `cmd_append`; leaving `out_len` untouched produces an empty response.
- Concurrency & dispatcher tasks:
  - Normal frames run on the work task (`CONFIG_ROUTER_WORK_TASK_PRIO` = 5); frames flagged with `ROUTER_FLAG_URGENT` run on the urgent task (`CONFIG_ROUTER_URGENT_TASK_PRIO` = 15).
  - Module routers must be thread-safe when accessing shared resources.
  - The watch's `urgent` (`router_set_watch`, after `router_wake_urgent`) executes on the urgent task.
- Links: one client per link, registered at boot (`router_link_register`, before or after `router_init`) and never removed. The answer goes back to the link the frame came from.
- Internal link & injection:
  - `ROUTER_LINK_INTERNAL` (0) is reserved for the board itself (`router_inject`). Injected frames run on router tasks, are not shown to the watch, and discard response payloads.
- Multi-packet answers & taps:
  - `router_answer_part` sends intermediate OK packets with matching `seq` before the final answer return.
  - `router_set_tap` provides global command inspection (`take` / `seen`), used by action recording (`sys_actions`).
  - `router_origin()` returns the command currently executing on the calling router task.
- Watch:
  - The router only reports link frames and the urgent wake to the watch (`router_watch_t`). What they mean (the dead-man, the safe state) lives in `sys_system` (SYS_SYSTEM.MD); keep it out of the router.

