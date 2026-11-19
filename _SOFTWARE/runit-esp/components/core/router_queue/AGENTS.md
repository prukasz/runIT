# router_queue

Spec: `ROUTER_QUEUE.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The flags (`ROUTER_FLAG_*`: the header's type byte carries them at the same bits, `CMD_TYPE_FLAGS` in cmd.h) and the answer status codes (`ROUTER_STATUS_OK` = 0, `ROUTER_STATUS_ERROR` = 1, bit 2 `CMD_TYPE_STATUS`) are wire format, checked against cmd.h at build time in `router_queue.h`; the sources (`CMD_SRC_*`) are cmd.h's. Do not renumber them. The ERROR answer's data is the error record of `errors` (ERRORS.MD "Error record"), built from the command's `cmd_req_t` (`err_record_cmd`).
- Error tags in `ERR_ROUTER_QUEUE_MAP` (`MODULE_ID_ROUTER_QUEUE`): the app and the scripts know them by name. Changing them requires `sh templates/sync.sh`. The route and handler errors are `router`'s.
- Commands and tasks:
  - The queue knows commands (`cmd_req_t`), not frames: every command comes with its `done` callback and `ctx`, and the queue keeps nothing of who sent it (no links, no state per peer: the session is the frames' owner, SYS_SESSION.MD). `router_submit` never blocks; what it can't queue it returns, and the caller answers it and reports it (`router_report_cmd`).
  - Normal commands run on the work task (`CONFIG_ROUTER_WORK_TASK_PRIO` = 5); commands flagged with `ROUTER_FLAG_URGENT` run on the urgent task (`CONFIG_ROUTER_URGENT_TASK_PRIO` = 15). Each task calls the command's `done` after it, from its own buffer, read-only for the callback. NOREPLY or `done` NULL: no answer; a failure is reported either way.
  - `router_run_urgent(fn)` runs `fn` once on the urgent task (`ERR_ROUTER_BUSY` while another waits).
- The board's own commands: `router_inject` submits with src `CMD_SRC_SELF`, seq from the queue's counter.
- Origin: `router_origin()` returns the command currently executing on the calling router task (a module reads its `src`); `router_is_task()` tells a router task.
- The queue runs urgent work (`router_run_urgent`); what it means (the safe state) lives in `sys_system` (SYS_SYSTEM.MD), whether a peer is alive in `sys_session`. Keep both out of the queue.
- `router_init` calls `router_root_init` (router.h) and starts the tasks.
- Host tests: `host/` (ROUTER_QUEUE.MD "Tests"). A change to the queues or the tasks is run with `python test_host/run.py router_queue` from the firmware project. The tests that run the tasks wait on latches, not on sleeps; keep it so (a test that must show "nothing ran yet" uses a short wait and says so).
