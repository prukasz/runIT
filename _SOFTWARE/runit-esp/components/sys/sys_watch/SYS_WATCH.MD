# sys_watch

Component `components/sys/sys_watch/`. A **watch** is a read command run on this board every `period` by a **trigger** (`PERIODIC`, `ON_CHANGE`) whose answers go out of the module's port `WATCH` (`DATA_MATRIX_PORT(MODULE, MODULE_ID_WATCH)`, 0x2B) as PUSH packets with the peer that added it (`owner`), to every port linked from `WATCH`: a session's sub-port, the raw BLE telemetry. The component also holds the **cached reads** of other boards made of such watches (`sys_watch_read` / `sys_watch_stale`).

## Files

| File | Role |
|---|---|
| `sys_watch.h` | The wire format (`sys_watch_head_t` 6 B, `sys_watch_info_t` 8 B, `sys_watch_trigger_e`, `SYS_WATCH_TICK_MS`, `SYS_WATCH_COPY_MAX` (= `ERR_RECORD_MAX`: the one bound of a kept entry), `SYS_WATCH_READ_MAX`), then the use: `sys_watch_init`, `sys_watch_read`, `sys_watch_stale` |
| `sys_watch.c` | The watches of this board: the table, the 10 ms tick (`board_timer`), `router_inject`, the entry (`[hdr PUSH, MODULE_ID_WATCH][owner][id][status][data]` out of `WATCH` to its links), `ERR_MODULE_DEFINE` |
| `sys_watch_client.c` | The cached reads: slots `FREE` / `ADDING` / `ACTIVE`, the B3 gate on ADD's OK, the idle tick |
| `internal/sys_watch_internal.h` | Between the sources and the router: `sys_watch_add` / `_remove` / `_list`, `sys_watch_client_init` |
| `sys_watch_id.h`, `errors/sys_watch_errors.h` | `MODULE_ID_DECLARE(MODULE_ID_WATCH, 0x2B)` (`sys_watch_id.h`), `ERR_SYS_WATCH_MAP`, `errors_sys_watch.json` |
| `router/sys_watch_router.h` / `.c` | `WATCH_OPERATIONS` (`OP_ADD`, `OP_REMOVE`, `OP_LIST`), `ROUTER_MODULE_REGISTER(MODULE_ID_WATCH, sys_watch_router)`, `contract_watch.json` |
| `host/` | Unity host tests (`test_sys_watch.c`) on fake links + real `sys_session`, `sys_event`, `router_queue`, `timer` |

## The watches of this board

A watch is added, removed and listed by commands (Remote); its owner is the command's source. It goes when its owner removes it (REMOVE), replaces it (ADD of its id), or its owner's session ends (`SESSION_EV_GONE`), and by itself only when an answer is too long for an entry. A watch of an owner with no session (a raw provider: MQTT) stays until REMOVE.

### Adding & triggers (`sys_watch_head_t`, 6 B)

- `id` (`1..255`): the owner's watch id. ADD with an `(owner, id)` that exists replaces that watch: a read of it still under way is dropped (the slot's `gen`).
- `trigger` (`sys_watch_trigger_e`):
  - `SYS_WATCH_PERIODIC` (`0`): runs the read every `period` ticks (`10 ms` per tick) and sends every answer.
  - `SYS_WATCH_ON_CHANGE` (`1`): runs every `period` ticks, hashes an OK answer with IEEE CRC-32, and sends it when the CRC differs from the last OK entry sent, after an error, or after `max_silence` ticks without an entry (`0` = never).
- `owner` is `router_origin()->src`: a board or a client (else `ERR_WATCH_BAD_ARG`). `read` is `2..SYS_WATCH_READ_MAX` bytes with `read[1] == CMD_DIR_READ` (else `ERR_WATCH_NOT_READ`).
- One `board_timer` runs every `SYS_WATCH_TICK_MS` (`10 ms`) from `sys_watch_init`. It starts the reads that are due with `router_inject`; one the router's queue refuses is tried again at the next tick.
- The entry goes out in the read's answer callback on the router's task, so a command and an entry keep their order on the link. An entry a line does not take (a busy provider) is lost and counted as sent: `PERIODIC` sends the next read, `ON_CHANGE` the next change or, after `max_silence`, the value again.
- An ERROR answer is sent once: the next errors are not sent until an OK entry goes.
- An answer too long for one entry (`3 + len > CMD_DATA_MAX`) removes the watch and is reported once (`ERR_CMD_OUT_FULL` with the read).
- A peer gone (`SESSION_EV_GONE`): its watches are removed (every watch for `CMD_SRC_ALL`).

### The entry (a PUSH packet of `WATCH`)

`[hdr PUSH, MODULE_ID_WATCH][owner u8][id u8][status u8][data ...]`, sent with `data_matrix_send(&WATCH, NOWAIT, ...)` to every port linked from it (the board's own: the console's session sub-port and the raw BLE telemetry, RUNIT.MD; a slave's session by its record); a line that does not take it loses it, the next entry comes with the next read. The client takes the entries on `WATCH`'s input (from the sessions' sub-ports linked to it), its own by `owner`, and the board is the packet's `src`.

- `owner`: the peer that added the watch. An entry goes to every port linked from `WATCH`, so whoever takes it keeps its own by `owner` (this board's client: `owner == board_id()`; the app: its client id).
- No per-watch sequence number (the `seq` of a PUSH is `sys_session`'s count per `(peer, class)`).
- `status == ROUTER_STATUS_OK` (`0`): the read's answer.
- `status == ROUTER_STATUS_ERROR` (`1`): the error record (`err_record_cmd`).

## Cached reads (`sys_watch_read` / `sys_watch_stale`)

A cached read is an `ON_CHANGE` watch this board keeps on another one, and the last entry of it.

1. **States:** `FREE -> ADDING -> ACTIVE -> FREE`, and `ACTIVE -> ADDING` when the watch has to be added again.
2. **The first read** `sys_watch_read(board, read, buf, cap, &len)` takes a `FREE` slot, sends `WATCH ADD` (`id = slot + 1`, `SYS_WATCH_ON_CHANGE`, period `CONFIG_SYS_WATCH_READ_PERIOD_MS`, `max_silence` twice that) and returns `ERR_WATCH_NO_VALUE`. While the slot is `ADDING`, a read sends ADD again when the last one went a period ago (it was refused, did not go, or its answer is late: ADD of an id that exists only replaces the watch).
3. **B3:** an entry `[id][status][data]` from `board` with `id == slot + 1` is taken only while the slot is `ACTIVE`, that is after ADD's OK: what came before it may be of the watch that had the id before.
4. **A read of an `ACTIVE` slot** returns the entry kept: its bytes, or its error as a chain under `ERR_REMOTE{board, 0}`. No entry yet: `ERR_WATCH_NO_VALUE`. An entry is kept up to `SYS_WATCH_COPY_MAX` (`ERR_RECORD_MAX`, err_record.h): a longer answer is `ERR_CMD_OUT_FULL{need, cap}`, never an old value nor "no value yet"; a buffer too small for it `ERR_CMD_BAD_LEN`.
5. **The owner checks that it receives:** the watch's board does not check where its entries go. An `ACTIVE` slot with nothing newer than `3 * CONFIG_SYS_WATCH_READ_PERIOD_MS` (no entry since ADD's OK, or the last one that old; the watch sends one at least every `max_silence`, twice the period) goes back to `ADDING` and sends ADD again; that read answers `ERR_WATCH_STALE` (an entry it had) or `ERR_WATCH_NO_VALUE` (none). So a watch gone on its board, or entries that do not reach this one, are added again every three periods and never served as fresh.
6. **After a write (`sys_watch_stale`):** the slots on `board` whose command has the write's router key and `target_len` bytes after `dir` go back to `ADDING` and send ADD again. ADD's OK comes after the write's answer, so every entry taken from then on was read after the write, and a watch just added sends its first read at once.
7. **A peer gone** (`SESSION_EV_GONE`): its slots go back to `ADDING`; the next read adds the watch again.
8. **Idle:** a slot not read for `CONFIG_SYS_WATCH_IDLE_MS` is freed and its watch removed with `WATCH REMOVE` (a 100 ms `board_timer`).

## Remote (`sys_watch_router`, root key `MODULE_ID_WATCH` `0x2B`)

| Op | Side | Args | Answer |
|---|---|---|---|
| `0x01` `WATCH_OP_ADD` | W | `sys_watch_head_t` (6 B) + `read` (`2..48 B`) | – |
| `0x02` `WATCH_OP_REMOVE` | W | `sys_watch_ref_t` (`u8 id`) | – |
| `0x03` `WATCH_OP_LIST` | R | – | `u8 count` + `count * sys_watch_info_t` (8 B each) |

## Errors (`MODULE_ID_WATCH` = `0x2B` / `43`)

| Tag | Code | Severity | Data | When |
|---|---|---|---|---|
| `ERR_WATCH_NOT_FOUND` | 1 | MEDIUM | `u8 id` | `REMOVE` for an unknown watch |
| `ERR_WATCH_NOT_READ` | 2 | MEDIUM | `u8 id` | `ADD` or `sys_watch_read` command has `dir != CMD_DIR_READ` |
| `ERR_WATCH_BAD_ARG` | 3 | MEDIUM | `u8 id` | Invalid `id`, `period`, `trigger`, `owner`/`board`, or command length |
| `ERR_WATCH_FULL` | 4 | MEDIUM | – | All watch (`CONFIG_SYS_WATCH_MAX`) or cached read (`CONFIG_SYS_WATCH_CLIENT_MAX`) slots taken |
| `ERR_WATCH_NO_VALUE` | 5 | LOW | `u8 board` | Cached read: ADD not answered yet, or no entry since |
| `ERR_WATCH_STALE` | 6 | MEDIUM | `u8 board, u16 age_ms` | Cached read: the entry kept is too old |

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_WATCH_MAX` | 24 | watches on this board, of every owner (at most 63: LIST in one frame) |
| `CONFIG_SYS_WATCH_CLIENT_MAX` | 24 | cached reads of other boards |
| `CONFIG_SYS_WATCH_READ_PERIOD_MS` | 100 | a cached read's watch period; ADD is sent again and an entry is stale by it |
| `CONFIG_SYS_WATCH_IDLE_MS` | 3000 | a cached read not read this long is removed |
