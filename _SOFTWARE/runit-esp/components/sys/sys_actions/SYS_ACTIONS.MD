# sys_actions — stored command lists

Component `components/sys/sys_actions/`. One store for everything the board keeps as commands. A **record** is a list of router frames stored in NVS under a 32-bit **ID**, replayed through `router_route`. A record is filled by **recording** a client's live frames (router tap) or by **uploading** the whole image, and read back frame by frame, exactly as stored.

The store holds four kinds of record, told apart by the class in the ID: the user's **actions**, the **settings** of the system modules (log, power, BLE, data streams), the **install and default commands** of each device, and the **project** (see [SYS_PROJECT.MD](../sys_project/SYS_PROJECT.MD)).

**Nothing replays by itself.** The VM or the app calls RUN. After a reset the board is in its default state (module defaults, devices off). A stored record changes nothing until it is run.

## Files

| File | Role |
|---|---|
| `sys_actions.h` | API, ID classes, system IDs, error tags, `g_sys_actions_tap` |
| `sys_actions.c` | NVS store, run, recorder (tap), upload |
| `sys_actions_router.c/.h` | `actions` router (`ROUTER_KEY_ACTIONS` = 0x06), op IDs `SYS_ACTIONS_OP_*` |
| `contract_actions.json` | Contract descriptor for the actions router |
| `Kconfig` | Partition, largest record, largest device record, nesting depth |

A change to `contract_actions.json`, to an op number, to an error tag, or to a marked `#ref-*` group here also changes what the app reads. Update `templates/` with it: `sh templates/sync.sh` regenerates `enums.json` and `errors.json`, refreshes `runit-app/generated/services/contract_actions.json`, and validates everything ([templates/AGENTS.md](../../../../templates/AGENTS.md)). Commit with `--check` green.

## Model

**ID = identity.** A u32 `[class u8][index u24]`; 0 = none (`ERR_ACTION_BAD_ID`). The client chooses it, except the system IDs, which are fixed in `sys_actions.h`.
| Class | `SYS_ACTIONS_CLASS_*` | Holds |
|---|---|---|
| `0x00` | USER | the app's actions (index 1..) |
| `0x01` | SYSTEM | settings of the system modules, one record each (below) |
| `0x02` | DEVICE | `0x02000000 \| device ID` (`SYS_ACTIONS_ID_DEVICE(dev)`, device ID u16): install (CREATE) and default commands |
| `0x03` | PROJECT | `0x03000000` the project record; the other IDs of this class belong to `sys_project` |

System IDs (class `0x01`):

| ID | Record | Set by |
|---|---|---|
| `0x01000001` `SYS_ACTIONS_ID_LOG` | log options (SET frame) | log |
| `0x01000002` `SYS_ACTIONS_ID_POWER_CFG` | power SET_CONFIG | power |
| `0x01000003` `SYS_ACTIONS_ID_POWER_PSU` | power SET_PSU | power |
| `0x01000004` `SYS_ACTIONS_ID_POWER_BATTERY` | power SET_BATTERY | power |
| `0x01000005` `SYS_ACTIONS_ID_POWER_RESPONSES` | power SET_RESPONSES (the whole matrix) | power |
| `0x01000006` `SYS_ACTIONS_ID_BLE` | Bluetooth GATT services and APPLY | BLE |
| `0x01000007` `SYS_ACTIONS_ID_CONN` | data streams and bindings | connector |
| `0x01000008` `SYS_ACTIONS_ID_POWER_PD` | power SET_PD (the USB-PD request) | power |

Any client may write any class and RUN any class. User IDs 3 to 6 are the firmware's own actions (RUNIT.MD "Onboard actions"): they are rewritten at every boot, so an app uses other IDs.

**Settings are records.** A settings record is the module's own SET frame (`[root key][op][args]`) saved by the app, e.g. the log options as `[0x08][0x01][level, mirror, trace, errors, logs]`. The module's SET applies the value live and stores nothing. Persistence is the app uploading (or recording) the record. A matrix of values is set whole (`SET_RESPONSES`).

**Image.** Frames back to back, each `[u16 len LE][keys + payload]`: what `router_route` takes (no seq, no flags), 1..`CONFIG_ROUTER_FRAME_MAX - 2` bytes. The total is at most `CONFIG_SYS_ACTIONS_MAX_BYTES` (4096), except a device-class record, which is at most `CONFIG_SYS_ACTIONS_MAX_BYTES_DEVICE` (512): the install and default commands of one device. A longer image is `ERR_ACTION_TOO_BIG {len, max}`, with the limit of its class (BEGIN, a recording, `sys_actions_set`). The CRC (`esp_rom_crc32_le(0, …)`, zlib / IEEE) covers the image, which is also what an upload sends.

**Storage.** Namespace `actions` in the `project` partition (`CONFIG_SYS_ACTIONS_PARTITION`, 512 KB), key `a<id>` (the ID as 8 hex digits, e.g. `a02000063`), one blob per record. One `nvs_set_blob` per store; NVS keeps the old value until the new one is complete. A partition NVS can't use (no free pages, new version) is erased at init.
The store checks framing only, not content. A frame the router can't take fails at its step (`ERR_ROUTER_UNKNOWN_KEY` for an unknown key).

**One session at a time.** A recording or an upload, built in one static buffer of `CONFIG_SYS_ACTIONS_MAX_BYTES`. Another start → `ERR_ACTION_BUSY`. Stop / commit closes the session even when they fail. ABORT drops it.

## Running

`sys_actions_run(id, keep_going)` loads the image and routes every frame in order. Answers are dropped. A failing frame's error is wrapped in `ERR_ACTION_STEP {id u32, step}` (step counted from 0), so the report shows the action, the step, the route and the cause.

- **Only on a router task** (`router_origin() != NULL`, else `ERR_ACTION_WRONG_TASK`). That means a remote RUN, or a RUN frame queued with `router_inject`. `sys_actions_request(id, urgent)` does the queueing for callers that aren't a router task (the VM, the device error policy). A replay never interleaves with live normal frames, since they wait on the same task. An urgent STOP still overtakes it.
- **RUN stops at the first failing frame. RUN_ALL does not** (`keep_going`): it runs every frame, reports each failure, and returns the first. A RUN_ALL inside a record continues inside it, but a failure it returns still stops an outer plain RUN.
- **Nesting.** A RUN inside a record nests up to `CONFIG_SYS_ACTIONS_MAX_DEPTH` (3, counted per router task); deeper → `ERR_ACTION_DEPTH`. A record that runs itself stops there. So a program can be a record of RUN frames: system records first, then devices, then code. The project upload (SYS_PROJECT.MD) sets that order; the firmware sorts nothing.
- Each nesting level holds its image plus one router frame of heap while it runs.

## Recording

`main` installs `g_sys_actions_tap` (`router_set_tap`, see [ROUTER.MD](../../core/router/ROUTER.MD)). RECORD_START records the client that sent it (its link, from `router_origin()`). Frames from other clients run as usual and are not recorded.

- Recorded: that client's frames, except the `sys` key (PING, STOP, …) and the actions ops other than RUN and RUN_ALL. A RUN or RUN_ALL is recorded as itself, not as its content.
- **`execute = 1`**: frames run as usual, and only those that succeed are recorded.
- **`execute = 0`**: frames are recorded and answered OK with no data, without running. This is how to build "motor forward" without the motor moving. A frame that would read something answers empty.
- A frame that doesn't fit ends the growth. RECORD_STOP then answers `ERR_ACTION_TOO_BIG` and stores nothing. No frame → `ERR_ACTION_EMPTY`.
- A `sys` frame (STOP) can't be recorded; upload it instead.

## Set from the firmware

`sys_actions_set(id, image, len)` stores a whole image without a session, so it works while a recording is open. The framing is checked like an upload's. It writes nothing when the same image is already stored, so there is no flash wear on every boot. Used for the firmware's onboard actions (`runit_onboard_actions.c`, see [RUNIT.MD](../../app/runit/RUNIT.MD)).

`sys_actions_copy(from, to)` copies a record (the limit of `to`'s class applies). `sys_actions_max_bytes(id)` gives the limit for an ID. `sys_actions_info` returns `{bytes, count, crc32}`.

**Reset and boot.** Onboard actions 5 (reset all) and 6 (hard reset) are a RESET sweep over the devices and a board restart. Reset all touches the devices only: it does not rewind the VM or re-apply a boot configuration. Hard reset restarts the board, which unloads the VM and the devices. Boot is not a record: the board's boot configuration is a device setting, and the boot step (`runit_board_devices_init`) creates the devices.

**Users.** `sys_project` builds the project upload on this store (set, copy, max_bytes, list, remove, and nested RUNs to depth 3, see [SYS_PROJECT.MD](../sys_project/SYS_PROJECT.MD)). `sys_actions_request` is called by the device error policy's action runner (`runit_error_policy.c`) and by the VM (`vm_exec.c`).

## Remote (`sys_actions_router`, root key `ROUTER_KEY_ACTIONS` = 0x06)

All IDs are u32 LE. `info` = `{u16 bytes, u16 count, u32 crc32}`.

| Op | Request | Response |
|---|---|---|
| `0x01` RUN | `u32 id` | – (error record on a failing step) |
| `0x02` RECORD_START | `u32 id, u8 execute` | – |
| `0x03` RECORD_STOP | – | info (stored under the recording's ID, replacing it) |
| `0x04` BEGIN | `u32 id, u16 len` | – |
| `0x05` WRITE | `u16 offset`, data | – (offset = bytes so far) |
| `0x06` COMMIT | `u32 crc32` | – (checks length, CRC and framing; stores, replacing the ID) |
| `0x07` READ | `u32 id` | **one answer packet per frame**, in order, each exactly the frame as recorded or uploaded (keys + payload, no length prefix). `x` frames in, `x` packets back, then nothing more. All carry the READ's `seq`; the client counts them with the `count` of INFO / RECORD_STOP. A failure (`ERR_ACTION_NOT_FOUND`, a frame that doesn't fit the link → `ERR_CMD_OUT_FULL`, a damaged image) is an error answer with that `seq` and ends the read |
| `0x08` INFO | `u32 id` | info |
| `0x09` LIST | `u32 start` | `u16 total` (all stored records), then u32 IDs ascending from `start`, up to 48 and as many as fit. Ask again from the last ID + 1 |
| `0x0A` REMOVE | `u32 id` | – |
| `0x0B` ABORT | – | – (drops an open recording or upload) |
| `0x0D` RUN_ALL | `u32 id` | – like RUN, but every frame runs. A failing frame is reported (error record, `ERR_ACTION_STEP`) and the run goes on; the first failure is the answer. Used to start a project, so one failing device leaves the rest configured. A record may contain RUN_ALL (it is recorded like RUN) |
| `0x0E` REMOVE_ALL | – | – (every stored record, of every class: settings, devices, project and user. `ERR_ACTION_BUSY` while a recording or upload is open. Stops at the first failing removal) |

`REMOVE` takes one ID. To clear one group only, list the IDs (`sys_actions_list`) and remove them one by one, as `sys_project`'s `clear_bank` does. REMOVE_ALL erases the stored records only. Devices and settings already running stay as they are until the board restarts; after a restart the board starts from its defaults, with no stored project and no device records.

READ is the mirror of recording and upload: the frames that went in come out, one packet each. The packets before the last go out with `router_answer_part` (see ROUTER.MD); the last one is the op's own answer. An answer has 3 header bytes where a request has 2, so a recorded frame of the largest size does not fit an answer (`ERR_CMD_OUT_FULL`). So does any frame longer than the link carries (BLE before the MTU exchange).

**Examples**
- Record "blink" as user action 5 without running it, over UART: `06 02 05 00 00 00 00` (RECORD_START id 5, execute 0), then the device frames, then `06 03` (RECORD_STOP → info). Run it: `06 01 05 00 00 00`.
- Save the log options as the system record: BEGIN `06 04 01 00 00 01 09 00`, WRITE the 9-byte image `07 00 08 01 03 01 00 01 01`, COMMIT with the CRC.

## Errors (`ERR_MOD_SYS_ACTIONS` = 8)

| Error | Severity |
|---|---|
| `ERR_ACTION_NOT_FOUND {id u32}` | MEDIUM |
| `ERR_ACTION_BAD_ID` (ID 0) | HIGH |
| `ERR_ACTION_BUSY` | MEDIUM |
| `ERR_ACTION_NO_SESSION` | MEDIUM |
| `ERR_ACTION_TOO_BIG {len, max}` | MEDIUM |
| `ERR_ACTION_OFFSET {got, want}` | MEDIUM |
| `ERR_ACTION_INCOMPLETE {got, want}` | MEDIUM |
| `ERR_ACTION_CRC {got, want}` (got = the client's) | MEDIUM |
| `ERR_ACTION_BAD_FRAME {offset}` | MEDIUM |
| `ERR_ACTION_EMPTY` | LOW |
| `ERR_ACTION_DEPTH {depth}` | MEDIUM |
| `ERR_ACTION_WRONG_TASK` | HIGH |
| `ERR_ACTION_NO_MEM {size}` | HIGH |
| `ERR_ACTION_STEP {id u32, step}` (context) | NONE |
| `ERR_ACTION_BAD_ARG` | HIGH |

NVS failures come as `ERR_ESP_ERR {code}`.

