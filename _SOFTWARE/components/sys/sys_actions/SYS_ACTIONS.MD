# sys_actions — stored command lists (for agents)

Component `components/sys/sys_actions/`. One store for everything the board keeps as commands: a **record** is a list of router commands stored in NVS under a 32-bit **ID**, replayed through `router_route`. It is filled by **recording** a client's live commands (router tap) or by **uploading** the whole image, and read back frame by frame, exactly as recorded. It holds the user's **actions**, the **settings** of the system modules (log, power) and the **install and default commands of each device**: the same engine, told apart by the class in the ID. Replaces legacy `sys_actions` (static C slots + recorded u8-ID actions, class `0x03`) and the short-lived name-keyed `sys_settings`.

**Nothing replays by itself.** The VM or the app calls RUN. After a reset the board is in its default state (module defaults, devices off); a stored record changes nothing until it is run.

## Files

| File | Role |
|---|---|
| `sys_actions.h` | API, the ID classes and system IDs, error tags, `g_sys_actions_tap` |
| `sys_actions.c` | NVS store, run, recorder (tap), upload |
| `sys_actions_router.c/.h` | `actions` router (`ROUTER_KEY_ACTIONS` = 0x06), op IDs `SYS_ACTIONS_OP_*` |
| `Kconfig` | Partition, largest record, nesting depth |

## Model

- **ID = identity**: u32 `[class u8][index u24]`, 0 = none (`ERR_ACTION_BAD_ID`). The client chooses it; the system IDs are fixed in `sys_actions.h`. No names: an ID is all a record has.

  | Class | `SYS_ACTIONS_CLASS_*` | Holds |
  |---|---|---|
  | `0x00` | USER | the app's actions (index 1..) |
  | `0x01` | SYSTEM | settings of the system modules: `0x01000001` log (the SET frame), `0x01000002` power SET_CONFIG, `…03` SET_PSU, `…04` SET_BATTERY, `…05` SET_RESPONSES (the whole matrix) |
  | `0x02` | DEVICE | per device, `0x02000000 \| device ID` (`SYS_ACTIONS_ID_DEVICE(dev)`): install (CREATE) and default commands. The board's own devices are created by the system, so their record has only the user's fields (enable, voltage, …) |

  | `0x03` | PROJECT | the stored project (SYS_PROJECT.MD): `0x03000000` the project record, `0x03<bank+1><n u16>` its program chunks and staged sections, numbered by the pipeline |

  Any client may write any class and RUN any class; the class only keeps the ranges apart.
- **Settings are records**: the module's own SET frame (`[root key][op][args]`) saved by the app, e.g. the log options as `[0x08][0x01][level, mirror, trace, errors, logs]`. The module's SET applies the value live and stores nothing; persistence is the app uploading (or recording) the record. A matrix of values (the power responses) has an op that takes it whole (`SET_RESPONSES`) next to the single-cell one.
- **Image**: frames back to back, `[u16 len LE][keys + payload]` — what `router_route` takes (no seq / flags), each 1..`CONFIG_ROUTER_FRAME_MAX - 2` bytes, at most `CONFIG_SYS_ACTIONS_MAX_BYTES` (4096) in total, except a device-class record, which is at most `CONFIG_SYS_ACTIONS_MAX_BYTES_DEVICE` (512): the install and default commands of one device. A longer image is `ERR_ACTION_TOO_BIG {len, max}` with the limit of its class (BEGIN, a recording, `sys_actions_set`). The image is what an upload sends and what the CRC (`esp_rom_crc32_le(0, …)` = zlib / IEEE) covers.
- **Storage**: namespace `actions` in the `project` partition (`CONFIG_SYS_ACTIONS_PARTITION`, 512 KB), key `a<id>` (the ID as 8 hex digits, e.g. `a02000063`), one blob = the image. One `nvs_set_blob` per store; NVS keeps the old value until the new one is complete. A partition NVS can't use (no free pages, new version) is erased at init. Records of the previous layout (`a007`, with a name field) and the old name-keyed settings are not read.
- **One session at a time**: a recording or an upload, built in one static buffer (`CONFIG_SYS_ACTIONS_MAX_BYTES`). Another start → `ERR_ACTION_BUSY`. Stop / commit closes the session also when they fail; ABORT drops it.

## Running

`sys_actions_run(id)`: loads the image, routes every frame in order, drops the answers, **stops at the first failing frame**: its chain is wrapped in `ERR_ACTION_STEP {id u32, step}` (step from 0), so the record shows the action, the step, the route and the cause.

- **Only on a router task** (`router_origin() != NULL`, else `ERR_ACTION_WRONG_TASK`): a remote RUN, or a RUN frame queued with `router_inject` — `sys_actions_request(id, urgent)` does that for callers that aren't a router task (the VM, the device error policy). So a replay never interleaves with live normal frames (they wait on the same task), and an urgent STOP still overtakes it.
- **RUN stops at the first failing frame; RUN_ALL does not** (`sys_actions_run(id, keep_going)`): it runs all of them, reports each failure and returns the first. A record that holds a RUN_ALL frame continues inside it, but a failure it returns stops an outer plain RUN.
- A RUN inside a record nests, up to `CONFIG_SYS_ACTIONS_MAX_DEPTH` (3, counted per router task) → `ERR_ACTION_DEPTH`. A record that runs itself stops there. So a program can be a record of `RUN` frames: first the system records, then the devices, then the code (the VM sets that order when it uploads a project; the firmware sorts nothing).
- Each level holds its image + one router frame of heap while it runs.

## Recording

`main` installs `g_sys_actions_tap` (`router_set_tap`, ROUTER.MD). RECORD_START records the client that sent it (link + peer from `router_origin()`); frames from other clients run as usual and aren't recorded.

- Recorded: that client's frames except the `sys` key (PING, STOP, …) and the actions ops other than RUN (a RUN is recorded as a RUN, not as its content).
- **`execute = 1`**: frames run as usual; only those that succeed are recorded.
- **`execute = 0`**: frames are recorded and answered OK with no data, without running (build "motor forward" without the motor moving). A frame that reads something answers empty.
- A frame that doesn't fit ends the growth; RECORD_STOP then answers `ERR_ACTION_TOO_BIG` and stores nothing. No frame → `ERR_ACTION_EMPTY`.
- A `sys` frame (STOP) can't be recorded; upload it.

## Set from the firmware

`sys_actions_set(id, image, len)` stores a whole image (framing checked like an upload's) without a session, so it works while a recording is open. It writes nothing when the same image is already stored (no flash wear on every boot). Used for the app's onboard actions (RUNIT.MD).

## Remote (`sys_actions_router`, root key `ROUTER_KEY_ACTIONS` = 0x06)

All IDs are u32 LE. `info` = `{u16 bytes, u16 count, u32 crc32}`.

| Op | Request | Response |
|---|---|---|
| `0x01` RUN | `u32 id` | – (error record on a failing step) |
| `0x02` RECORD_START | `u32 id, u8 execute` | – |
| `0x03` RECORD_STOP | – | info (stored under the recording's ID, replacing it) |
| `0x04` BEGIN | `u32 id, u16 len` | – |
| `0x05` WRITE | `u16 offset`, data | – (offset = bytes so far) |
| `0x06` COMMIT | `u32 crc32` | – (checks length, CRC, framing; stores, replacing the ID) |
| `0x07` READ | `u32 id` | **one answer packet per frame**, in order, each exactly the frame as recorded or uploaded (keys + payload, no length prefix): `x` frames in, `x` packets back, then nothing more. All carry the READ's `seq`; the client counts them with the `count` of INFO / RECORD_STOP. A failure (`ERR_ACTION_NOT_FOUND`, a frame that doesn't fit the link: `ERR_CMD_OUT_FULL`, a damaged image) is an error answer with that `seq` and ends it |
| `0x08` INFO | `u32 id` | info |
| `0x09` LIST | `u32 start` | `u16 total` (all stored records), then u32 IDs ascending from `start`, up to 48 and as many as fit; ask again from the last ID + 1 |
| `0x0A` REMOVE | `u32 id` | – |
| `0x0B` ABORT | – | – (drops an open recording / upload) |
| `0x0D` RUN_ALL | `u32 id` | – like RUN, but every frame runs: a failing frame is reported (error record, `ERR_ACTION_STEP`) and the run goes on; the first failure is the answer. For starting a project, so one failing device leaves the rest configured. A record may contain RUN_ALL (it is recorded like RUN) |

`0x0C` was RENAME (names are gone) and is never reused.

READ is the mirror of recording and upload: the same frames that went in come out, one packet each. The packets before the last are sent with `router_answer_part` (ROUTER.MD), the last is the op's own answer. An answer has 3 header bytes where a request has 2, so a recorded frame of exactly the largest size does not fit an answer (`ERR_CMD_OUT_FULL`); so does any frame longer than the link carries (BLE before the MTU exchange).

Example — record "blink" as user action 5 without running it, over UART: `06 02 05 00 00 00 00` (RECORD_START id 5, execute 0), the device frames, `06 03` (RECORD_STOP → info). Run it: `06 01 05 00 00 00`. Save the log options as the system record: BEGIN `06 04 01 00 00 01 09 00`, WRITE the 9-byte image `07 00 08 01 03 01 00 01 01`, COMMIT the CRC.

## Errors (`ERR_MOD_SYS_ACTIONS` = 8)

`ERR_ACTION_NOT_FOUND {id u32}` MEDIUM, `_BAD_ID` HIGH (ID 0), `_BUSY` MEDIUM, `_NO_SESSION` MEDIUM, `_TOO_BIG {len, max}` MEDIUM, `_OFFSET {got, want}` MEDIUM, `_INCOMPLETE {got, want}` MEDIUM, `_CRC {got, want}` MEDIUM (got = the client's), `_BAD_FRAME {offset}` MEDIUM, `_EMPTY` LOW, `_DEPTH {depth}` MEDIUM, `_WRONG_TASK` HIGH, `_NO_MEM {size}` HIGH, `_STEP {id u32, step}` NONE (context), `_BAD_ARG` HIGH. Tags 2 (BAD_NAME) and 13 (READ_RANGE) are retired. NVS failures come as `ERR_ESP_ERR {code}`.

## Changes from legacy (`legacy/components/system/sys_actions`)

- One ID space (u32, classes) and no names; no static (C) actions and no scope byte: the legacy sweeps (resume / suspend all) are ordinary router ops a record contains (SUSPEND / RESUME to device `0xFFFF`, SYS_DEVICE.MD); reset all is not there (see Open). The firmware stores its own with `sys_actions_set` (runit "Onboard actions", RUNIT.MD): resume all (3) and suspend all (4), the legacy IDs, in the user class.
- Upload and frame-by-frame readback (legacy could only record).
- Record without executing; only successful frames recorded when executing (legacy recorded every frame).
- Replay on a router task instead of suspending RX; urgent frames overtake it.
- Stored in the `project` partition, not `nvs`; 4 KB per record instead of 2 KB.
- Class `0x03` → `actions` router.

## Open

- `reset_all` and the legacy reset / hard-reset / boot actions (5, 6, 1): need a `sys_device` RESET sweep and the VM, not ported. Suspend / resume all exist (device `0xFFFF`).
- The project upload is `sys_project` (SYS_PROJECT.MD), on top of this store: it uses `sys_actions_set`, `sys_actions_copy`, `sys_actions_max_bytes`, `sys_actions_list` / `remove` and nested RUNs (depth 3). Callers of `sys_actions_request` today: the device error policy's action runner (`runit_error_policy.c`).
- No remove-all op (legacy had one). No schema check: a record stored by older firmware replays its frames as they are (the router answers unknown keys / bad lengths with errors).
