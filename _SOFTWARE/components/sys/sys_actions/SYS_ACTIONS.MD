# sys_actions — stored command lists (for agents)

Component `components/sys/sys_actions/`. An **action** is a list of router commands stored in NVS under an **ID** (1..255) with a **name** next to it, replayed through `router_route`. It is filled by **recording** a client's live commands (router tap) or by **uploading** the whole image, and read back as the whole image. Replaces legacy `sys_actions` (static C slots + recorded u8-ID actions, class `0x03`).

## Files

| File | Role |
|---|---|
| `sys_actions.h` | API, error tags, `g_sys_actions_tap` |
| `sys_actions.c` | NVS store, run, recorder (tap), upload |
| `sys_actions_router.c/.h` | `actions` router (`ROUTER_KEY_ACTIONS` = 0x06), op IDs `SYS_ACTIONS_OP_*` |
| `Kconfig` | Partition, largest action, nesting depth |

## Model

- **ID = identity**: u8, 1..255 (`SYS_ACTIONS_ID_NONE` = 0 = no action, `ERR_ACTION_BAD_ID`). Everything that refers to an action (RUN, later event rules and the VM `ACTION` block) carries the ID; the client chooses it.
- **Name = label**: 0..15 printable ASCII characters (`SYS_ACTIONS_NAME_MAX` = 16 with the NUL), may be empty, not unique. Given with RECORD_START / BEGIN, changed with RENAME, shown by INFO and LIST.
- **Image**: frames back to back, `[u16 len LE][keys + payload]` — what `router_route` takes (no seq / flags), each 1..`CONFIG_ROUTER_FRAME_MAX - 2` bytes, at most `CONFIG_SYS_ACTIONS_MAX_BYTES` (4096) in total. The image is what READ returns, what an upload sends and what the CRC (`esp_rom_crc32_le(0, …)` = zlib / IEEE) covers; the name is not part of it.
- **Storage**: namespace `actions` in the `project` partition (`CONFIG_SYS_ACTIONS_PARTITION`, 512 KB), key `a<id>` (3 digits, e.g. `a007`), one blob `[name, 16 B NUL-padded][image]`. One `nvs_set_blob` per store, so name and frames always change together; NVS keeps the old value until the new one is complete. A partition NVS can't use (no free pages, new version) is erased at init.
- **One session at a time**: a recording or an upload, built in one static buffer (16 + `CONFIG_SYS_ACTIONS_MAX_BYTES`). Another start → `ERR_ACTION_BUSY`. Stop / commit closes the session also when they fail; ABORT drops it.

## Running

`sys_actions_run(id)`: loads the image, routes every frame in order, drops the answers, **stops at the first failing frame**: its chain is wrapped in `ERR_ACTION_STEP {id, step}` (step from 0), so the record shows the action, the step, the route and the cause.

- **Only on a router task** (`router_origin() != NULL`, else `ERR_ACTION_WRONG_TASK`): a remote RUN, or a RUN frame queued with `router_inject` — `sys_actions_request(id, urgent)` does that for callers that aren't a router task (events, the VM, boot). So a replay never interleaves with live normal frames (they wait on the same task), and an urgent STOP still overtakes it.
- A RUN inside an action nests, up to `CONFIG_SYS_ACTIONS_MAX_DEPTH` (3, counted per router task) → `ERR_ACTION_DEPTH`. An action that runs itself stops there.
- Each level holds its blob + one router frame of heap while it runs.

## Recording

`main` installs `g_sys_actions_tap` (`router_set_tap`, ROUTER.MD). RECORD_START records the client that sent it (link + peer from `router_origin()`); frames from other clients run as usual and aren't recorded.

- Recorded: that client's frames except the `sys` key (PING, STOP, …) and the actions ops other than RUN (a RUN is recorded as a RUN, not as its content).
- **`execute = 1`**: frames run as usual; only those that succeed are recorded.
- **`execute = 0`**: frames are recorded and answered OK with no data, without running (build "motor forward" without the motor moving). A frame that reads something answers empty.
- A frame that doesn't fit ends the growth; RECORD_STOP then answers `ERR_ACTION_TOO_BIG` and stores nothing. No frame → `ERR_ACTION_EMPTY`.
- A `sys` frame (STOP) can't be recorded; upload it.

## Remote (`sys_actions_router`, root key `ROUTER_KEY_ACTIONS` = 0x06)

`name` = NUL-ended with nothing after the NUL, `"\0"` alone = no name (`ERR_CMD_BAD_ARG` otherwise); `info` = `{u16 bytes, u16 count, u32 crc32}` then the name.

| Op | Request | Response |
|---|---|---|
| `0x01` RUN | `u8 id` | – (error record on a failing step) |
| `0x02` RECORD_START | `u8 id, u8 execute`, name | – |
| `0x03` RECORD_STOP | – | info (stored under the recording's ID, replacing it) |
| `0x04` BEGIN | `u8 id, u16 len`, name | – |
| `0x05` WRITE | `u16 offset`, data | – (offset = bytes so far) |
| `0x06` COMMIT | `u32 crc32` | – (checks length, CRC, framing; stores, replacing the ID) |
| `0x07` READ | `u8 id, u16 offset, u16 len` | the image bytes (≤ what the link carries) |
| `0x08` INFO | `u8 id` | info |
| `0x09` LIST | `u8 start` | `u16 total`, then `{u8 id, name}` by ascending ID from `start`, as many as fit; ask again from the last ID + 1 |
| `0x0A` REMOVE | `u8 id` | – |
| `0x0B` ABORT | – | – (drops an open recording / upload) |
| `0x0C` RENAME | `u8 id`, name | – (frames unchanged) |

Example — record action 5 "blink" without running it, over UART: `06 02 05 00 62 6C 69 6E 6B 00` (RECORD_START id 5, execute 0, "blink"), the device frames, `06 03` (RECORD_STOP → info). Run it: `06 01 05`.

## Errors (`ERR_MOD_SYS_ACTIONS` = 8)

`ERR_ACTION_NOT_FOUND {id}` MEDIUM, `_BAD_ID` HIGH (ID 0), `_BAD_NAME` HIGH, `_BUSY` MEDIUM, `_NO_SESSION` MEDIUM, `_TOO_BIG {len, max}` MEDIUM, `_OFFSET {got, want}` MEDIUM, `_INCOMPLETE {got, want}` MEDIUM, `_CRC {got, want}` MEDIUM (got = the client's), `_BAD_FRAME {offset}` MEDIUM, `_EMPTY` LOW, `_DEPTH {depth}` MEDIUM, `_WRONG_TASK` HIGH, `_READ_RANGE {offset, len, size}` MEDIUM, `_NO_MEM {size}` HIGH, `_STEP {id, step}` NONE (context), `_BAD_ARG` HIGH. NVS failures come as `ERR_ESP_ERR {code}`.

## Changes from legacy (`legacy/components/system/sys_actions`)

- One ID space (1..255) with a name label; no static (C) actions and no scope byte: the legacy sweeps (resume / suspend / reset all) become ordinary router ops a stored action can contain (not there yet, see Open).
- Upload, full-image read and rename (legacy could only record).
- Record without executing; only successful frames recorded when executing (legacy recorded every frame).
- Replay on a router task instead of suspending RX; urgent frames overtake it.
- Stored in the `project` partition, not `nvs`; 4 KB per action instead of 2 KB.
- Class `0x03` → `actions` router.

## Open

- Sweep ops in the `sys` router (`suspend_all`, `resume_all`, `reset_all`) so actions can replace the legacy static slots.
- Callers of `sys_actions_request`: event rules (done, SYS_EVENT.MD); the VM `ACTION` block and a boot action not ported.
- No remove-all op (legacy had one).
- No schema check: an action stored by older firmware replays its frames as they are (the router answers unknown keys / bad lengths with errors).
