# sys_session

Spec: `SYS_SESSION.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The header (`cmd_hdr_t`, 5 B = `CMD_HEAD`, its type byte, kinds and ids: cmd.h, CMD.MD "The header"), HELLO's body and the control ops (`sys_session.h`), the ports' names (`SYS_SESSION_PORT`), the router's ops and frames and the error tags (`MODULE_ID_SESSION` 0x0E) are wire format: never renumber or re-layout them without changing, in the same change, `contract_session.json`, `runit-esp/tools/session_client.py` and the app's session code.
- A session adds little: its ports, this board's id as `src` when a frame has none, a PUSH's `seq`, its own control frames, the dead-man, the events. It never looks into a REQUEST, an ANSWER or a PUSH's data, never answers for the router, never keeps a frame: what comes in goes out of a port at once.
- Data plane = the data matrix only: frames reach the line out of the session port, and modules out of its sub-ports (`data_matrix_send`); no other way to a provider, no hook of a module. Which module gets what is the links', never the session's.
- No routing, no task, no queue: a port's input runs on the sender's task and never blocks (the control frames `NOWAIT`); the tick runs on the board timers' task.
- The sessions are under the spinlock of `sys_session.c`. Under it: copy what is needed out, never send, publish or call the router.
- `seq` of a PUSH is the session's count of its PUSH frames in that class (`push_seq[urgent]`): never a `seq` from a module, never a counter per stream. No receive counters here: the gap is the receiver's to read.
- `src` is set once: by a sub-port's input to this board's id when it is `CMD_SRC_NONE`, never changed when it is set. A REQUEST or an ANSWER from the line is the peer's own: one with another `src` is dropped, never passed on.
- A session has no kind: no master, slave or client in the code, only an id from its hello and its own dead-man time and safety flag. The id kinds (0x00..0xEF a board, 0x00 none given; 0xF0..0xFC a client) are checked in the hello and nowhere else.
- Anything but HELLO and NO_SESSION on a session not open here is answered NO_SESSION and the board's hello (`start_over`). It is the protocol's own recovery: nothing is reported.
- The calls are the router's (`router_request`): the session only gives the port of a peer (`sys_session_port`) and fails the calls through a session that ends (`router_request_fail_to`).
- A board answers a hello only when it opens a session (a client every hello), so two boards do not answer each other for ever.
- A session goes only by DETACH: nothing it notices (a timeout, a provider down or removed) detaches it; it ends and waits. The provider's state comes from its events (UP: the hello; DOWN, REMOVED: the end; CREATED: the links again) and `sys_data_provider_state` at the attach; nothing else of the provider is read.
- The board's identity (its id, boot id, build) is `board_id`'s (BOARD_ID.MD): the session reads it, never keeps or saves its own. Nothing here includes ESP-IDF headers beyond FreeRTOS.
- A session tells who is reached and gone by events (`sys_session_events.h`), never by a hook table: a module that needs it subscribes inline. A session is made by ATTACH (the board's record) or `sys_session_attach` at boot: nothing else attaches or detaches one.
- The dead-man is the session's own time (attach, `sys_session_deadman_set`); a session without it never ends by silence. The safety flag only goes with `SESSION_EV_GONE`: what it does (the safe state) is `sys_system`'s.
- Keep it small: no state that only guards against a caller's mistake, no limit or counter nobody reads, no second path. A control frame a full queue refuses is lost; the hello and the ping repeat.
- Four headers, no more: `sys_session.h` (wire format and use), `router/sys_session_router.h`, `errors/sys_session_errors.h`, `internal/sys_session_internal.h`.
