# sys_session

Spec: `SYS_SESSION.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The header (`sys_session_hdr_t`, 5 B = `CMD_HEAD`), the hello (`sys_session_hello_t`, 24 B), the frame byte's bits, the kinds, the control ops, the ids, the peer kinds, the access, `SYS_SESSION_VERSION`, the router ops and the error tags (`ERR_MOD_SYS_SESSION` = 18) are wire format: never renumber or re-layout them. A change runs `sh templates/sync.sh` and moves `SYS_SESSION_VERSION` when the frame changes; the app (`runit-app/src/backend/protocol/frames.ts`, `session/link.ts`) and `router_link.py` follow.
- The flags in the frame byte are the router's own (`ROUTER_FLAG_*`): checked at build time in `sys_session.c`.
- No task, no queue: what comes in is handled on the driver's RX task, which never blocks (sends with `NOWAIT`, `router_submit`). Only `session_answer` (a router task) waits for room in a provider's queue.
- The sessions are under one spinlock: never send, call a listener or submit under it.
- A client's frames carry the id the board gave it: anything else before or outside its session is answered NO_SESSION and the board's hello.
- The dead-man is a gone listener of `sys_system`, not this module's: a session ends whether it is armed or not.
