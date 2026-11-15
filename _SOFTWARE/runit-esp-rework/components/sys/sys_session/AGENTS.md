# sys_session

Spec: `SYS_SESSION.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The header (`sys_session_hdr_t`, 3 B = `CMD_HEAD`), HELLO's body (both in `sys_session.h`), the frame byte's bits, the kinds, the channels, the control ops, the ids, the router's ops and frames and the error tags (`MODULE_ID_SESSION` 0x0E) are wire format: never renumber or re-layout them without changing, in the same change, `contract_session.json`, `runit-esp-rework/tools/session_client.py` and the app's session code.
- The flags in the frame byte are the router's own (`ROUTER_FLAG_*`): checked at build time in `sys_session.c`.
- No task, no queue: what comes in is handled on the driver's RX task, which never blocks (sends with `NOWAIT`, `router_submit`); the tick runs on the board timers' task. Only the router's answer callback waits for room in a provider's queue.
- No routing: a frame goes on the session of the peer it is for (`sys_session_route`: the open session with that id) and on no other, with the send flags of its frame byte (`send_flags`: `NOWAIT`, `URGENT`, `SPARE` for a PUSH); a frame this board makes is joined by `send` (header, a control op, body). A frame for another board is a command of the board in between, never a relay here. No other wrapper around a send.
- The sessions are under the spinlock of `sys_session.c`, the call slots under the one of `call/`; the two are never held together. Under either: copy what is needed out, never send, call a listener, a call's callback or submit.
- `seq` of a PUSH is the session's count of its PUSH frames in that class (`push_seq[urgent]` of the session): never a `seq` from a module, never a counter per stream. No receive counters here: the gap is the receiver's to read.
- A session has no kind: no master, slave or client in the code, only an id from its hello and its own dead-man time and safety flag. The hello has no time: the silence limit and the keepalive (a third of it) are the session's own. The id kinds (0x00..0xEF a board, 0xF0..0xFC a client) are checked in the hello and nowhere else.
- Anything but HELLO and NO_SESSION on a session not open here is answered NO_SESSION and the board's hello (`start_over`). It is the protocol's own recovery: nothing is reported.
- A call ends once: the slot is taken out under the lock by whoever ends it (`take_call`: the answer by its `seq`, the tick, the session ending, a waiter past its own limit), and only then is its callback called. A waiter waits on the `done` flag its callback sets last, never on the notification alone, and always has a limit of its own.
- A board answers a hello only when it opens a session (a client, an id in 0xF0..0xFC, every hello), so two boards do not answer each other for ever.
- ESP-IDF's identity (NVS, `esp_random`, the app's description) is only in `board/`; the host tests give their own, so nothing else here may include ESP-IDF headers beyond FreeRTOS.
- A session tells who is reached and gone by events (`sys_session_events.h`), never by a hook table: a module that needs it subscribes inline. Only the PUSH taker (`sys_session_set_push`, the data matrix) is a hook (a PUSH carries up to `CMD_DATA_MAX`, an event 64 B). A session is made by the matrix's role: nothing else attaches or detaches one.
- The dead-man is the session's own time (`attach`, `sys_session_deadman_set`); a session without it never ends by silence. The safety flag only goes with `SESSION_EV_GONE`: what it does (the safe state) is `sys_system`'s, not this module's.
- `host/test_sys_session.c` covers the handshake, the independent sessions, the broadcast, the dead-man, the push `seq`, the calls and the wait: a change of any of them adds its case there.
- Keep it small: no state that only guards against a caller's mistake, no limit or counter nobody reads, no second path for what `send` or `start_over` already does. A control frame a full queue refuses is lost; the hello and the ping repeat.
- Four headers, no more: `sys_session.h` (wire format and use), `router/sys_session_router.h`, `errors/sys_session_errors.h`, `internal/sys_session_internal.h`.
