# sys_watch

Component `components/sys/sys_watch/`. A **watch** is a read command run on this board by a **trigger** (`PERIODIC`, `ON_CHANGE`, `ON_EVENT`) whose answers are sent to the peer that added it (`owner`) as session PUSH frames on channel `SYS_SESSION_CHANNEL_WATCHES` (`0x80`). This component also contains the **watch client** for cache-only remote reads (`sys_watch_read` / `sys_watch_stale`).

## Files

| File | Role |
|---|---|
| `sys_watch.h` | Public API: `sys_watch_init`, server `add` / `remove` / `list`, cache-only client `read` / `stale` |
| `sys_watch.c` | Server table, `board_timer` tick (`SYS_WATCH_TICK_MS`), `sys_event` listener, `router_inject`, PUSH encoding (`[0x80][id][status][data]`), `MODULE_ID_DEFINE(WATCH, 0x2B)`, `ERR_MODULE_DEFINE` |
| `sys_watch_client.c` | Reworked cache-only client (state machine `FREE` / `ADDING` / `ACTIVE` / `OLD`, B3 gate on ADD OK reply, `board_timer` idle cleanup) |
| `config/sys_watch_config.h` | Wire formats: `sys_watch_head_t` (14 B), `sys_watch_info_t` (16 B), `sys_watch_trigger_e`, `SYS_WATCH_TICK_MS` / `_COPY_MAX` / `_READ_MAX` |
| `errors/sys_watch_errors.h` | `MODULE_ID_DECLARE(WATCH, 0x2B)` (`ERR_MOD_SYS_WATCH`), `ERR_SYS_WATCH_MAP`, `errors_sys_watch.json` |
| `router/sys_watch_router.h` / `.c` | `WATCH_OPERATIONS` (`OP_ADD`, `OP_REMOVE`, `OP_LIST`), `ROUTER_MODULE_REGISTER(WATCH, sys_watch_router)`, `contract_watch.json` |
| `internal/sys_watch_internal.h` | `sys_watch_client_init` |
| `host/` | Unity host tests (`test_sys_watch.c`) on fake links + real `sys_session`, `sys_event`, `router_queue`, `timer` |

## Server (watches running on this board)

### Adding & triggers (`sys_watch_head_t`, 14 B)

- `id` (`1..255`): caller's watch id. Adding an existing `(owner, id)` replaces it, unsubscribing any prior `ON_EVENT` handle and bumping the slot's generation so in-flight answers from the replaced watch are discarded.
- `trigger` (`sys_watch_trigger_e`):
  - `SYS_WATCH_PERIODIC` (`0`): runs the read every `period` ticks (`10 ms` per tick) and sends every answer.
  - `SYS_WATCH_ON_CHANGE` (`1`): runs every `period` ticks, hashes OK answers with IEEE CRC-32, and pushes when the CRC differs from the last sent OK entry, on recovery from an error, or after `max_silence` silent ticks (`0` = never).
  - `SYS_WATCH_ON_EVENT` (`2`): subscribes `head->filter` (`sys_event_key_t`, 8 B) via `sys_event_subscribe(..., SYS_EVENT_RUN_QUEUED, ...)`, marking `pending` on matching events and running at most once per `period` ticks.
- `owner` comes from `router_origin()->src` (`SYS_SESSION_ID_SELF`, `_NONE`, and `_BROADCAST` are rejected with `ERR_WATCH_BAD_ARG`). `read` must have length `2..SYS_WATCH_READ_MAX` with `read[1] == CMD_DIR_READ` (else `ERR_WATCH_NOT_READ`).
- One `board_timer` runs every `SYS_WATCH_TICK_MS` (`10 ms`) only while `count > 0`.
- Reads execute via `router_inject` and push inside `watch_done` on `router_work_task`, preserving command-then-entry order on the link. Failed injects and failed reads emit a serialized error record once (`err_sent` deduplication) until an OK read succeeds.
- When a peer disconnects (`sys_session_on_gone`), all watches owned by that peer (or all watches on `SYS_SESSION_ID_BROADCAST`) are removed, unsubscribed, and freed.

### PUSH entry wire format (channel `0x80`)

Each watch entry is sent with `sys_session_push(owner, frame, 3 + len, 0)`:

`[0x80 SYS_SESSION_CHANNEL_WATCHES][id u8][status u8][data ...]`

- No `owner` byte (the PUSH frame's destination peer `dst` is already its owner).
- No per-watch sequence number (`sys_session` tracks `(peer, class)` PUSH counters).
- `status == ROUTER_STATUS_OK` (`0`): raw read answer bytes.
- `status == ROUTER_STATUS_ERROR` (`1`): serialized error record (`cmd_err_record`).

## Client (`sys_watch_read` / `sys_watch_stale`)

The reworked client keeps only cache-only remote reads over `ON_CHANGE`:

1. **States:** `FREE -> ADDING -> ACTIVE <-> OLD -> FREE`.
2. **First read** `sys_watch_read(board, read, buf, cap, &len)` allocates a `FREE` slot, enters `ADDING`, sends `WATCH ADD` (`id = slot + 1`, `SYS_WATCH_ON_CHANGE`, period `CONFIG_SYS_WATCH_READ_PERIOD_MS`, `max_silence = 2x`), and returns `ERR_WATCH_NO_VALUE`.
3. **B3 guard:** PUSH frames `[0x80][id][status][data]` matching `(src == board, id == slot + 1)` are ignored while the slot is `ADDING` (until `ADD` answers `ROUTER_STATUS_OK`) or `OLD` (until the post-write refresh completes).
4. **Fresh hit / staleness / error:** Once `ACTIVE` holds a valid entry at most `3 * CONFIG_SYS_WATCH_READ_PERIOD_MS` old, `sys_watch_read` returns the cached bytes (or parses an error entry wrapped in `ERR_REMOTE{board, 0}`). Older entries return `ERR_WATCH_STALE`. Answers longer than `SYS_WATCH_COPY_MAX` (`32 B`) cannot be cached.
5. **Write invalidation (`sys_watch_stale`):** Matches active/old slots on `board` sharing the router key and `target_len` bytes after `dir`, transitions them to `OLD` (bumping `gen`, dropping pre-write data), and starts a direct `sys_session_call` refresh read.
6. **Peer gone & idle cleanup:** `sys_session_on_gone` moves affected slots back to `ADDING` (with `add_in_flight = false`) so the next read re-adds the watch. A `board_timer` idle check removes slots unread for `max(30 * CONFIG_SYS_WATCH_READ_PERIOD_MS, CONFIG_SYS_WATCH_IDLE_MIN_MS)` via `WATCH REMOVE`.

## Remote (`sys_watch_router`, root key `MODULE_ID_WATCH` `0x2B`)

| Op | Side | Args | Answer |
|---|---|---|---|
| `0x01` `WATCH_OP_ADD` | W | `sys_watch_head_t` (14 B) + `read` (`2..48 B`) | – |
| `0x02` `WATCH_OP_REMOVE` | W | `sys_watch_ref_t` (`u8 id`) | – |
| `0x03` `WATCH_OP_LIST` | R | – | `u8 count` + `count * sys_watch_info_t` (16 B each) |

## Errors (`ERR_MOD_SYS_WATCH` = `0x2B` / `43`)

| Tag | Code | Severity | Data | When |
|---|---|---|---|---|
| `ERR_WATCH_NOT_FOUND` | 1 | MEDIUM | `u8 id` | `REMOVE` for an unknown watch |
| `ERR_WATCH_NOT_READ` | 2 | MEDIUM | `u8 id` | `ADD` or `sys_watch_read` command has `dir != CMD_DIR_READ` |
| `ERR_WATCH_BAD_ARG` | 3 | MEDIUM | `u8 id` | Invalid `id`, `period`, `trigger`, `owner`/`board`, or command length |
| `ERR_WATCH_FULL` | 4 | MEDIUM | – | All server (`CONFIG_SYS_WATCH_MAX`) or client (`CONFIG_SYS_WATCH_CLIENT_MAX`) slots taken |
| `ERR_WATCH_NO_VALUE` | 5 | LOW | `u8 board` | Cached read pending initial `ADD` / first entry / post-write refresh |
| `ERR_WATCH_STALE` | 6 | MEDIUM | `u8 board, u16 age_ms` | Latest cached entry exceeded age limit |
