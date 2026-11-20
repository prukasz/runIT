# router_queue

Spec: `ROUTER_QUEUE.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The flags (`ROUTER_FLAG_*`: the header's type byte carries them at the same bits, `CMD_TYPE_FLAGS` in cmd.h) and the answer status codes (`ROUTER_STATUS_OK` = 0, `ROUTER_STATUS_ERROR` = 1, bit 2 `CMD_TYPE_STATUS`) are wire format, checked against cmd.h at build time in `router_queue.h`; the sources (`CMD_SRC_*`) are cmd.h's. Do not renumber them. The ERROR answer's data is the error record of `errors` (ERRORS.MD "Error record"), built from the command's `cmd_req_t` (`err_record_cmd`).
- Error tags in `ERR_ROUTER_QUEUE_MAP` (`MODULE_ID_ROUTER_QUEUE`): the app and the scripts know them by name. Changing them requires `sh templates/sync.sh`. The route and handler errors are `router`'s.
- Commands and tasks:
  - A command comes from the port (a frame, its answer back to the port it came from: `data_matrix_send_to`) or from this board's C (`router_inject`, its `done` callback and `ctx`); the queue keeps nothing of who sent it (no registry of links, no state per peer). `router_inject` never blocks; what it can't queue it returns. The port's `in` never blocks either: it queues, or answers ERROR at once with `NOWAIT`.
  - The port is linked with the sessions' command sub-ports and the providers that carry commands: never with another port whose `in` sends (DATA_MATRIX.MD "A send inside an `in`").
  - This board's calls (`router_request.c`): a call ends once, taken out of its slot under the calls' lock by whoever ends it (the answer by its `seq` and port, the timer, `router_request_fail_to`, a waiter past its own limit), and only then is its callback called. A waiter waits on the `done` flag its callback sets last, never on the notification alone, and always has a limit of its own. Under the calls' lock: copy out, never send or call back.
  - Normal commands run on the work task (`CONFIG_ROUTER_WORK_TASK_PRIO` = 5); commands flagged with `ROUTER_FLAG_URGENT` run on the urgent task (`CONFIG_ROUTER_URGENT_TASK_PRIO` = 15). Each task calls the command's `done` after it, from its own buffer, read-only for the callback. NOREPLY or `done` NULL: no answer; a failure is reported either way.
  - `router_run_urgent(fn)` runs `fn` once on the urgent task (`ERR_ROUTER_BUSY` while another waits).
- The board's own commands: `router_inject` submits with src `CMD_SRC_SELF`, seq from the queue's counter.
- Origin: `router_origin()` returns the command currently executing on the calling router task (a module reads its `src`); `router_is_task()` tells a router task.
- The queue runs urgent work (`router_run_urgent`); what it means (the safe state) lives in `sys_system` (SYS_SYSTEM.MD), whether a peer is alive in `sys_session`. Keep both out of the queue.
- The two tasks start in a constructor (static, before `app_main`); `router_init` calls `router_root_init` (router.h), registers the port and starts the calls' timer.
- Host tests: `host/` (ROUTER_QUEUE.MD "Tests"). A change to the queues or the tasks is run with `python test_host/run.py router_queue` from the firmware project. The tests that run the tasks wait on latches, not on sleeps; keep it so (a test that must show "nothing ran yet" uses a short wait and says so).
