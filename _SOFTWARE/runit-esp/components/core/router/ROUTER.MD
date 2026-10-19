# router — remote command routing (for agents)

Component `components/core/router/`. Every remote command (phone, test host, internal injection) arrives on a **link**, is queued by the **dispatcher**, and is routed through a tree of **routers**: a root table of keys, then each module's own router.

## Files

| File | Role |
|---|---|
| `router.h` | Wire format, error tags, routing helpers, links, tap, watch. The handler type `cmd_f`, `cmd_reply` and `ERR_CMD_*` are in `components/core/cmd` (CMD.MD) |
| `router_ids.h` | Root keys (`ROUTER_KEY_*`) |
| `router.c` | Root table, `router_route`, `router_call` / `router_args` / `router_args_tail` (op macros in `router.h`) |
| `router_dispatch.c` | Links, `router_submit`, urgent + work tasks, responses, tap, watch |
| `router_err.c` | `router_err_record` — error record (request + chain) |
| `Kconfig` | Frame size, response stream byte, table sizes, queues, task prio/stack |
| `AGENTS.md` | Rules for changing this folder |

## Wire format

```
request   [seq][flags][root key][key]...[payload]
response  [0x05][seq][status][data]        (CONFIG_ROUTER_RESPONSE_STREAM)
```

- `seq`: client's counter, echoed; the client matches answers by it (answers may come out of order — urgent frames overtake).
- `flags` bit 0 `ROUTER_FLAG_URGENT`: the app decides what is urgent; the firmware has no fixed list.
- `status` 0 OK: `data` = router output (may be empty). 1 error: `data` = the **error record** (below): the request and its error chain together, so a captured or stored error is self-contained.
- Payloads are packed little-endian structs, size checked exactly (`router_args`).

## Error record

`router_err_record(cmd, err, buf, cap)` (`router_err.c`). Used for every error response and, through the error sink in `runit` (RUNIT.MD), for every record on the errors stream (SYS_CONNECTOR.MD).

```
[src u8]                                    0 internal (no request), 1 command
[unix_ms u64 LE]                            the board's clock when it was reported (ROUTER_ERR_STAMP_BYTES); before 2020: not a date, a time since the start
src 1: [link u8][seq u8][flags u8]               (after the stamp)
       [req_len u8]  bits 0-6 = request bytes that follow, bit 7 = request cut
       [request]     keys + payload as received (no seq / flags)
[chain]                                     err_encode(): [count|bit7 cut] then [tag u32][sev][len][payload] per node
```

- The request is cut to `CONFIG_ROUTER_ERR_REQ_MAX` (32) bytes, and further when the link's frame is small; the chain gets the rest (its own truncated bit).
- The chain starts with the route (`ERR_ROUTER_KEY{key}` per level, NONE severity) and ends with the root cause, so it shows how far routing got; the request shows what was asked.
- Command errors end in `err_report_ex(err, ctx_line, &cmd)`: the log gets a context line before the chain (`cmd uart seq 7 flags 00 req 02 02 00`) and the sink gets the command as origin, so the errors stream carries the same record as the response.
- `src 0` (internal: device task, timer, boot) has no command header.

Example — scan of bus 0 (only bus 1 is open):

```
request   07 00 | 02 02 00                           seq 7, normal, i2c SCAN bus 0
response  05 07 01                                   stream, seq, ERROR
          01 | 00 00 00 00 00 00 00 00                   command; clock not set (time since the start, here 0)
          01 | 07 | 00                                UART, seq 7, flags 0
          03 | 02 02 00                               request: 3 B
          03                                          chain: 3 nodes
          01 00 02 00 00 01 02                        ERR_ROUTER_KEY        NONE  key 02 (i2c)
          01 00 02 00 00 01 02                        ERR_ROUTER_KEY        NONE  key 02 (SCAN)
          10 00 01 00 02 01 00                        ERR_I2C_REMOTE_DENIED MEDIUM bus 0
UART      #R:0507010100000000000000000107000302020003010002000001020100020000010210000100020100
```

## Routing

- A module router is a `cmd_f` (CMD.MD): `in` starts at **this router's key**. A module router consumes its key and calls `router_call(table, n, ctx, in + 1, in_len - 1, …)`, which looks up the next key, calls that entry with the same `ctx`, and wraps a failure in `ERR_ROUTER_KEY{key}`. So every level shows in the error chain.
- **Writing ops**: the native C API stays the source (typed, no packing on internal calls, callbacks and handles); an op only adapts it. Use the macros, not hand-written `cmd_f`s:
  - `ROUTER_OP(fn, args_t) { … }`: fixed packed payload, the body gets `args` (`const args_t*`).
  - `ROUTER_OP_NOARGS(fn) { … }`: no payload.
  - `ROUTER_OP_TAIL(fn, hdr_t) { … }`: header plus a variable tail (`tail`, `tail_len` ≥ 1), e.g. a NUL-ended name or data bytes.
  - `ROUTER_MODULE(fn, k_ops)`: writes the module's `cmd_f` (prefix `static` for a file-local one); `ROUTER_MODULE_DECLARE(fn);` declares it in the module's `*_router.h`.
  Bodies also see `ctx`, `out`, `out_cap`, `out_len` (answer with `cmd_reply`). The length check (`ERR_CMD_BAD_LEN`) is done before the body runs. Remote-only policy (e.g. `ERR_I2C_REMOTE_DENIED`) belongs in the body, not in the native API.
- Root: `router_register(key, fn)` at boot (before `router_init`), from the app layer (`main`). Modules never register themselves; a module only exports its `cmd_f` (e.g. `sys_i2c_router` in `sys_i2c_router.h`).
- `out_cap` is limited to what the origin link carries right now; answers use `cmd_reply` (`ERR_CMD_OUT_FULL` when too big).
- `router_route(in, …)` is also the entry point for replaying stored action frames.
- **Concurrency**: urgent and normal frames run concurrently on two tasks, so module routers must be thread-safe.

## Dispatcher

```
link RX task ── router_submit(link, frame) ──► copy into ring buffer
                    ├─ flags & URGENT ─► urgent ring ─► urgent task (prio 15)
                    └─ else ───────────► work ring ───► work task (prio 5)
                                                         └─► router_route ─► answer ─► link->send
```

- Links: `router_link_register(id, &{name, send, max_frame})` at boot, never removed. One client per link: the answer goes back to the link. `send` must be thread-safe (both tasks answer).
- `router_submit` copies the frame into the ring buffer and never blocks. A frame that cannot be queued (bad length: `ERR_ROUTER_FRAME_LEN`, ring full: `ERR_ROUTER_QUEUE_FULL`) is answered with an error right away and reported with its command; `router_submit` then returns NULL.
- Each task has its own static response buffer. Errors of routed frames are answered (error record) and reported with their command (log + errors stream).

### Internal link, tap, origin

- **Link 0 (`ROUTER_LINK_INTERNAL`)** is the board itself. `router_inject(req, len, urgent)` queues keys + payload (seq from an internal counter, flags from `urgent`) on the same rings and tasks as link frames; answers are dropped (the internal link has no `send`), errors are still reported (link `internal`). Injected frames are not shown to the watch (they prove no client is alive). Task context, never blocks (`ERR_ROUTER_QUEUE_FULL`, `ERR_ROUTER_FRAME_LEN`). `router_submit` refuses link 0.
- **Tap** `router_set_tap(&{take, seen})` (boot, by the app): `take(cmd)` runs before routing — true = answered OK with no data and not routed; `seen(cmd, result)` after routing a frame that was not taken. Both run on the routing task for every link. Used by the actions recorder (SYS_ACTIONS.MD).
- **Origin** `router_origin()`: the command the calling task is routing (link, seq, request), NULL outside the router tasks; inside a replay it is still the outer command.
- **More than one answer** `router_answer_part(out, len)`: sends the first `len` bytes of the op's own `out` buffer as an extra OK answer (same `seq` and link) before the op returns. A command that answers with several packets sends all but the last this way and returns the last (actions READ: one packet per recorded frame). The client matches by `seq` and knows the count; an error answer ends the series.

### Watch

- `router_set_watch(&{frame, urgent})` (boot, before `router_init`; one watch, members may be NULL): what the links tell about their client, for the module that watches it (the dead-man of `sys_system`, SYS_SYSTEM.MD). The router does not know what it means.
- `frame()`: a frame from a link (not link 0) arrived, before it is queued; on the link's task.
- `urgent()`: run on the urgent task after `router_wake_urgent()` (any task or a timer callback, not an ISR), for work that may block (entering the safe state).

## Errors (`ERR_MOD_ROUTER` = 2)

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_ROUTER_KEY` | NONE | `u8 key` | Route step context (wraps underlying handler error) |
| `ERR_ROUTER_NO_KEY` | HIGH | – | Input ended where a key was expected |
| `ERR_ROUTER_UNKNOWN_KEY` | HIGH | `u8 key` | No registered handler for key in table |
| `ERR_ROUTER_KEY_TAKEN` | HIGH | `u8 key` | Root key already registered |
| `ERR_ROUTER_TABLE_FULL` | HIGH | `u8 key` | Root table capacity (`CONFIG_ROUTER_MAX_KEYS`) reached |
| `ERR_ROUTER_BAD_LINK` | HIGH | `u8 link` | Invalid link ID, unregistered link or slot taken |
| `ERR_ROUTER_QUEUE_FULL` | MEDIUM | `u8 link, u8 urgent` | Inbound ring buffer full; frame dropped |
| `ERR_ROUTER_FRAME_LEN` | MEDIUM | `u8 link, u16 len, u16 max` | Frame too short (< 3 B) or exceeds link max length |

Payload errors `ERR_CMD_BAD_LEN`, `_OUT_FULL`, `_BAD_ARG` belong to `ERR_MOD_CMD` = 11 (CMD.MD).

## Root keys and links

| Key | Router | Doc |
|---|---|---|
| `0x01` sys | `sys_system_router` | SYS_SYSTEM.MD (ping, stop, restart, dead-man, clock, safe state) |
| `0x02` i2c | `sys_i2c_router` | SYS_I2C.MD "Remote" |
| `0x03` ble | `sys_ble_router` | SYS_BLE.MD "Remote" |
| `0x04` conn | `sys_connector_router` | SYS_CONNECTOR.MD "Remote" |
| `0x05` device | `sys_device_router` | SYS_DEVICE.MD: `[id u16][op u16][args]` |
| `0x06` actions | `sys_actions_router` | SYS_ACTIONS.MD |
| `0x07` event | – (no router) | frame shape of the events stream: `[0x07][0x01 NOTIFY][sys_event_t]`, SYS_EVENT.MD |
| `0x08` log | `sys_log_router` | SYS_LOG.MD (log options SET / GET) |
| `0x09` | – | Reserved (unused) |
| `0x0A` power | `sys_power_manager_router` | SYS_POWER_MANAGER.MD |
| `0x0B` vm | `vm_router` | VM_CORE.MD (build, run and watch a program) |
| `0x0C` project | `sys_project_router` | SYS_PROJECT.MD: save a whole project as one stream, run, info, erase |

| Link (transport ID) | Component |
|---|---|
| `0` internal | the router itself (`router_inject`) |
| `1` UART | `sys/sys_uart` (SYS_UART.MD) — `#R:` hex lines |
| `2` BLE | `sys/sys_ble` (SYS_BLE.MD) — RX 0xFFE2 / TX 0xFFE1 |

## Limits

- `CONFIG_ROUTER_FRAME_MAX` (default 512 bytes): maximum frame length accepted and produced.
- `CONFIG_ROUTER_MAX_KEYS` (default 16): maximum registered root keys.
- `CONFIG_ROUTER_MAX_LINKS` (default 4): maximum registered transport links (IDs 1..4).
- Inbound ring buffers: `CONFIG_ROUTER_WORK_QUEUE_SIZE` (default 4 KB) and `CONFIG_ROUTER_URGENT_QUEUE_SIZE` (default 1 KB).
- Error request truncation: `CONFIG_ROUTER_ERR_REQ_MAX` (default 32 bytes).
- Concurrency: urgent task (priority 15) and work task (priority 5) run concurrently; module routers must be thread-safe.

## Tests

Host test script `router_tests.py` (in `.claude/skills/runit-esp/scripts/`) covers:
- The sys key (SYS_SYSTEM.MD): PING (uptime), STOP (safe state), DEADMAN_SET / GET, BYE, TIME_SET / GET, RESTART, SAFE_GET / SAFE_LEAVE.
- Key dispatching, nested router calls, error chain recording with route keys.
- Urgent task preemption over work queue.
- Frame sizing, queue saturation, and error records.
