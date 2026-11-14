# sys_session

Spec: `SYS_SESSION.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The header (`sys_session_hdr_t`, 5 B = `CMD_HEAD`), HELLO's and PEER's bodies (`frame/sys_session_frame.h`), the frame byte's bits, the kinds, the channels, the control ops, the ids, the ways, the roles, the router's ops and frames and the error tags (`MODULE_ID_SESSION` 0x0E) are wire format: never renumber or re-layout them without changing, in the same change, `contract_session.json`, `runit-esp-rework/tools/session_client.py` and the app's session code.
- The flags in the frame byte are the router's own (`ROUTER_FLAG_*`): checked at build time in `sys_session.c`.
- No task, no queue: what comes in is handled on the driver's RX task, which never blocks (sends with `NOWAIT`, `router_submit`); the tick runs on the board timers' task. Only the router's answer callback waits for room in a provider's queue.
- Every frame goes out as two parts (header, body) through `sys_session_send_parts`; a forwarded one as one part, as it came. Never build a frame in a buffer of its own. A PUSH carries `SPARE` (`sys_session_send_flags`); an answer and a control frame never do.
- The sessions are under the spinlock of `sys_session.c`, the call slots under the one of `call/`; the two are never held together (a call takes its route, then its slot). Never send, call a listener, a call's callback or submit under either.
- `seq` of a PUSH is the session's counter of (peer, class) (`push_hdr`, `push_lost`), DATA included: never a `seq` from a module, never a counter per stream. A full table sends `UNCOUNTED`, never refuses. An entry starts again when its peer is reached or gone (a session opening or ending, the master's PEER): a new place that reaches or loses a peer clears its entry too.
- A peer's role is computed from the way and the hello (`sys_session_role`), never stored; the master is the open `UP` session with a board's id (`master_at`), never a field.
- A client's and a slave's frames carry its own id; anything else, and anything before or after its session, is answered NO_SESSION and the board's hello (`sys_session_start_over`), reported once per closing.
- A frame goes by its `dst` (only the root relays, never from a slave to a slave); the rest of it is never read or changed. A broadcast goes up to the root, which runs it and sends it down.
- A call ends once: the slot is taken out under the lock by whoever ends it (the answer, the tick, the session ending, a waiter past its own limit), and only then is its callback called. A waiter waits on the `done` flag its callback sets last, never on the notification alone, and always has a limit of its own.
- A board answers a hello only when it opens a session (a client every hello), so two boards do not answer each other for ever.
- ESP-IDF's identity (NVS, `esp_random`, the app's description) is only in `board/`; the host tests give their own, so nothing else here may include ESP-IDF headers beyond FreeRTOS.
- The dead-man is a gone listener of `sys_system`, not this module's: a session ends whether it is armed or not.
- `host/test_sys_session.c` covers the handshake, the star, the forwarding, the push counters, the calls and the wait: a change of any of them adds its case there.
