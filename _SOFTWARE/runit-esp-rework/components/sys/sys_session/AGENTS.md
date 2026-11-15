# sys_session

Spec: `SYS_SESSION.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The header (`sys_session_hdr_t`, 5 B = `CMD_HEAD`), HELLO's and PEER's bodies (all in `sys_session.h`), the frame byte's bits, the kinds, the channels, the control ops, the ids, the peers, the router's ops and frames and the error tags (`MODULE_ID_SESSION` 0x0E) are wire format: never renumber or re-layout them without changing, in the same change, `contract_session.json`, `runit-esp-rework/tools/session_client.py` and the app's session code.
- The flags in the frame byte are the router's own (`ROUTER_FLAG_*`): checked at build time in `sys_session.c`.
- No task, no queue: what comes in is handled on the driver's RX task, which never blocks (sends with `NOWAIT`, `router_submit`); the tick runs on the board timers' task. Only the router's answer callback waits for room in a provider's queue.
- One way out: the route gives a provider (`sys_session_route`: the master's, else the open session with `dst`), and a frame goes to `sys_data_provider_send` with the send flags of its frame byte (`send_flags`: `NOWAIT`, `URGENT`, `SPARE` for a PUSH). A forwarded frame goes as it came; a frame this board makes is joined by `send` (header, a DATA frame's head, body). No other wrapper around a send.
- The sessions are under the spinlock of `sys_session.c`, the call slots under the one of `call/`; the two are never held together. Under either: copy what is needed out, never send, call a listener, a call's callback or submit.
- `seq` of a PUSH is this board's count of its PUSH frames to that peer in that class (`push_seq[peer][urgent]`), DATA included: never a `seq` from a module, never a counter per stream. The array is by peer id: nothing fills, nothing is cleared. No receive counters here: the gap is the receiver's to read.
- A peer is computed from ATTACH's `slave` and the hello (`role_of`), never stored; the master is the open session not told as a slave with a board's id (`master`), never a field. ATTACH tells only `SLAVE` or `UNKNOWN`: a board's hello can't tell its master from its slave.
- A client's and a slave's frames carry its own id; anything else, and anything but HELLO and NO_SESSION on a session not open here, is answered NO_SESSION and the board's hello (`start_over`). It is the protocol's own recovery: nothing is reported.
- A frame goes by its `dst` (only the root relays, never from a slave to a slave); the rest of it is never read or changed. A broadcast is run and sent on to every board next to this one but the one it came from (`spread`).
- A call ends once: the slot is taken out under the lock by whoever ends it (`take_call`: the answer by its `seq`, the tick, the session ending, a waiter past its own limit), and only then is its callback called. A waiter waits on the `done` flag its callback sets last, never on the notification alone, and always has a limit of its own.
- A board answers a hello only when it opens a session (a client every hello), so two boards do not answer each other for ever.
- ESP-IDF's identity (NVS, `esp_random`, the app's description) is only in `board/`; the host tests give their own, so nothing else here may include ESP-IDF headers beyond FreeRTOS.
- The dead-man is a gone listener of `sys_system`, not this module's: a session ends whether it is armed or not.
- `host/test_sys_session.c` covers the handshake, the star, the forwarding, the broadcast, the push `seq`, DATA, the calls and the wait: a change of any of them adds its case there.
- Keep it small: no state that only guards against a caller's mistake, no limit or counter nobody reads, no second path for what `send` or `start_over` already does. A control frame a full queue refuses is lost; the hello and the ping repeat.
- Four headers, no more: `sys_session.h` (wire format and use), `router/sys_session_router.h`, `errors/sys_session_errors.h`, `internal/sys_session_internal.h`.
