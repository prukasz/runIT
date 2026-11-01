# router

Spec: `ROUTER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Root keys in `router_ids.h` (`ROUTER_KEY_*`), the flags (`ROUTER_FLAG_*`: the session's frame byte carries them at the same bits), the answer status codes (`ROUTER_STATUS_OK` = 0, `ROUTER_STATUS_ERROR` = 1) and `ROUTER_SRC_SELF` (0xFE, `SYS_SESSION_ID_SELF`) are wire format. Do not renumber them. The ERROR answer's data is the error record of `errors` (ERRORS.MD "Error record"), built from the command's `err_cmd_t`.
- Error tags in `ERR_ROUTER_MAP` (`ERR_MOD_ROUTER` = 2) are wire format: do not renumber them. Changing them requires `sh templates/sync.sh` (regenerates `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green. Payload and direction errors (`ERR_CMD_BAD_LEN`, `_OUT_FULL`, `_BAD_ARG`, `_DIRECTION`) belong to `cmd` (`ERR_MOD_CMD` = 11).
- Root registration: `router_register(key, fn)` occurs at boot before `router_init()`. Modules do not register themselves; the application (`runit.c`) registers module routers.
- The direction byte after the root key (`[key][dir]`, ROUTER.MD "A command") is wire format: every request has it, stream frames don't.
- Command tables and op macros:
  - A module's commands are a `router_op_t` table of `ROUTER_R` / `ROUTER_W` / `ROUTER_RW` rows; `ROUTER_MODULE` runs it (`router_call_op`: the side `dir` asks for, a failure wrapped in `ERR_ROUTER_KEY{code}`). A command with both sides follows the mirror (CMD.MD "Read and write").
  - Write each side with `ROUTER_OP`, `ROUTER_OP_NOARGS` or `ROUTER_OP_TAIL`: they parse payloads and enforce exact size matching via `router_args` / `router_args_tail`.
  - Handlers write responses using `cmd_reply` or `cmd_append`; `out->len` 0 is an empty response. One answer per command: no op sends extra packets.
- Commands and tasks:
  - The router knows commands (`err_cmd_t`), not frames: every command comes with its `done` callback and `ctx`, and the router keeps nothing of who sent it (no links, no state per peer: the session is the frames' owner, SYS_SESSION.MD). `router_submit` never blocks; what it can't queue it returns, and the caller answers it and reports it (`router_report_cmd`).
  - Normal commands run on the work task (`CONFIG_ROUTER_WORK_TASK_PRIO` = 5); commands flagged with `ROUTER_FLAG_URGENT` run on the urgent task (`CONFIG_ROUTER_URGENT_TASK_PRIO` = 15). Each task calls the command's `done` after it, from its own buffer (`CMD_HEAD` bytes of room in front of the data). NOREPLY or `done` NULL: no answer; a failure is reported either way.
  - Module routers must be thread-safe when accessing shared resources.
  - `router_run_urgent(fn)` runs `fn` once on the urgent task (`ERR_ROUTER_BUSY` while another waits).
- The board's own commands: `router_inject` submits with src `ROUTER_SRC_SELF`, seq from the router's counter.
- Taps and origin:
  - `router_set_tap` provides global command inspection (`take` / `seen`), used by action recording (`sys_actions`).
  - `router_origin()` returns the command currently executing on the calling router task (a module reads its `src`); `router_is_task()` tells a router task.
- The router runs urgent work (`router_run_urgent`); what it means (the safe state) lives in `sys_system` (SYS_SYSTEM.MD), whether a peer is alive in `sys_session`. Keep both out of the router.

