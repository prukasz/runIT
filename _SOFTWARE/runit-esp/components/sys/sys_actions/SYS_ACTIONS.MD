# sys_actions — stored command lists

Component `components/sys/sys_actions/`. One store for everything the board keeps as commands. A **record** is a list of router frames stored in NVS under a 32-bit **ID**, replayed through `router_route`. A client writes a record whole (BEGIN, WRITE, COMMIT) and reads it back by offset, the image exactly as stored.

The store holds four kinds of record, told apart by the class in the ID: the user's **actions**, the **settings** of the system modules (log, power, BLE, data streams), the **install and default commands** of each device, and the **project** (see [SYS_PROJECT.MD](../sys_project/SYS_PROJECT.MD)).

**Nothing replays by itself.** The VM or the app calls RUN. After a reset the board is in its default state (module defaults, devices off). A stored record changes nothing until it is run.

## Files

| File | Role |
|---|---|
| `sys_actions.h` | API, ID classes, system IDs |
| `errors/sys_actions_errors.h` | `ERR_SYS_ACTIONS_MAP`, the module's error tags (included by `sys_actions.h`) |
| `errors/errors_actions.json` | The error messages |
| `sys_actions.c` | NVS store, run, writing a record, the onboard actions |
| `router/sys_actions_router.c/.h` | `actions` router (`ROUTER_KEY_ACTIONS` = 0x06), op IDs `SYS_ACTIONS_OP_*`. The header publishes every op (`SYS_ACTIONS_OP_*`, with its sides) and the wire layouts, each op above its frame |
| `router/contract_actions.json` | Contract descriptor for the actions router |
| `Kconfig` | Partition, largest record, nesting depth |

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
| `0x01000001` `SYS_ACTIONS_ID_LOG` | log options (the CONFIG write) | log |
| `0x01000002` `SYS_ACTIONS_ID_POWER_CFG` | power CONFIG | power |
| `0x01000003` `SYS_ACTIONS_ID_POWER_PSU` | power PSU | power |
| `0x01000004` `SYS_ACTIONS_ID_POWER_BATTERY` | power BATTERY | power |
| `0x01000005` `SYS_ACTIONS_ID_POWER_RESPONSES` | power MATRIX (the response matrix) | power |
| `0x01000006` `SYS_ACTIONS_ID_BLE` | Bluetooth GATT services and APPLY | BLE |
| `0x01000007` `SYS_ACTIONS_ID_STREAM` | data streams and bindings | stream |
| `0x01000008` `SYS_ACTIONS_ID_POWER_PD` | power PD (the USB-PD request) | power |

Class `0xFF` holds no records: an error policy's `actions[]` use it for the policy's own responses (`SYS_DEVICE_RESPONSE_*`, SYS_DEVICE.MD "Error policy"). Any client may write any other class and RUN any class. User IDs 3 to 6 are the firmware's own actions ("Onboard actions" below): they are rewritten at every boot, so an app uses other IDs.

**Settings are records.** A settings record is the module's own write frame (`[root key][0x01 write][op][args]`) saved by the app, e.g. the log options as `[0x08][0x01][0x01][level, mirror, trace, errors, logs]` (log CONFIG). The write applies the value live and stores nothing. Persistence is the app writing the record.

**Image.** Frames back to back, each `[u16 len LE][key][dir][…]`: what `router_route` takes (no seq, no flags; the dir stays), 1..`CMD_DATA_MAX` (507) bytes (CMD.MD "Frame size"). The total is at most `CONFIG_SYS_ACTIONS_MAX_BYTES` (4096), of every class. A longer image is `ERR_ACTION_TOO_BIG {len, max}` (BEGIN, `sys_actions_set`). The CRC (`esp_rom_crc32_le(0, …)`, zlib / IEEE) covers the image, which is also what a client sends.

**Storage.** Namespace `actions` in the `project` partition (`CONFIG_SYS_ACTIONS_PARTITION`, 512 KB), key `a<id>` (the ID as 8 hex digits, e.g. `a02000063`), one blob per record. One `nvs_set_blob` per store; NVS keeps the old value until the new one is complete. A partition NVS can't use (no free pages, new version) is erased at boot (SYS_NVS.MD).
The store checks framing only, not content. A frame the router can't take fails at its step (`ERR_ROUTER_UNKNOWN_KEY` for an unknown key).

**One draft at a time.** A draft is a record being written: the image on the heap, from BEGIN to COMMIT or ABORT. Another BEGIN → `ERR_ACTION_BUSY`. COMMIT closes the draft even when it fails.

## Running

`sys_actions_run(id, keep_going)` loads the image and routes every frame in order, each with its own dir. Answers are dropped. A failing frame's error is wrapped in `ERR_ACTION_STEP {id u32, step}` (step counted from 0), so the report shows the action, the step, the route and the cause.

- **Only on a router task** (`router_origin() != NULL`, else `ERR_ACTION_WRONG_TASK`). That means a remote RUN, or a RUN queued with `router_inject`. `sys_actions_request(id, urgent)` does the queueing for callers that aren't a router task (the VM, the device error policy). A replay never interleaves with live normal commands, since they wait on the same task. An urgent STOP still overtakes it.
- **RUN stops at the first failing frame. RUN_ALL does not** (`keep_going`): it runs every frame, reports each failure, and returns the first. A RUN_ALL inside a record continues inside it, but a failure it returns still stops an outer plain RUN.
- **Nesting.** A RUN inside a record nests up to `CONFIG_SYS_ACTIONS_MAX_DEPTH` (3, counted per router task); deeper → `ERR_ACTION_DEPTH`. A record that runs itself stops there. So a program can be a record of RUN frames: system records first, then devices, then code. The project upload (SYS_PROJECT.MD) sets that order; the firmware sorts nothing.
- Each nesting level holds its image plus one router frame of heap while it runs.

## Set from the firmware

`sys_actions_set(id, image, len)` stores a whole image without a draft, so it works while a client writes another record. The framing is checked like a commit's. It writes nothing when the same image is already stored, so there is no flash wear on every boot. Used for the firmware's onboard actions (below).

`sys_actions_copy(from, to)` copies a record. `sys_actions_info` returns `{bytes, count, crc32}`.

**Reset and boot.** Onboard actions 5 (reset all) and 6 (hard reset) are a RESET sweep over the devices and a board restart. Reset all touches the devices only: it does not rewind the VM or re-apply a boot configuration. Hard reset restarts the board, which unloads the VM and the devices. The boot runs two records by itself, each by its own module: the board's profile (class `0x04`, SYS_BOARD.MD), which creates the board's devices, and the stored project as its boot option says (SYS_PROJECT.MD).

**Users.** `sys_project` builds the project upload on this store (set, copy, list, remove, and nested RUNs to depth 3, see [SYS_PROJECT.MD](../sys_project/SYS_PROJECT.MD)). `sys_actions_request` is called by the device error policy's action runner (`runit_error_policy.c`) and by the VM (`vm_exec.c`).

## Onboard actions

The firmware's own actions, written by `sys_actions_init` at every boot with `sys_actions_set` (only when they differ, so a client's edit of these IDs is replaced). Their IDs are fixed, `SYS_ACTIONS_ONBOARD_*` in `sys_actions.h`, published as the group `onboard_action` of `enums.json`, so the app keeps its own actions off them. Each is one write frame: a device-router sweep to device `0xFFFF` (SYS_DEVICE.MD "Sweeps over the router"), or a sys command.

| ID | Does | Frame |
|---|---|---|
| 3 `SYS_ACTIONS_ONBOARD_RESUME` (user class, `0x00000003`) | resume all | `05 01 FF FF 03 00` (every device, oldest first) |
| 4 `SYS_ACTIONS_ONBOARD_SUSPEND` (`0x00000004`) | suspend all | `05 01 FF FF 02 00` (every device, newest first) |
| 5 `SYS_ACTIONS_ONBOARD_RESET` (`0x00000005`) | reset all devices | `05 01 FF FF 01 00` (every device, newest first; the drivers' RESET) |
| 6 `SYS_ACTIONS_ONBOARD_HARD_RESET` (`0x00000006`) | hard reset: restart the board | `01 01 03` (sys RESTART, `SYS_SYSTEM_OP_RESTART`, ~100 ms after the answer) |

Run with `06 01 01 <id u32>` (actions RUN, a write), or name the ID in a device's error policy. Hard reset is a board restart, so the VM, the devices and the BLE link are all rebuilt by the boot (and the project's boot option, if set). Reset all does not rewind the VM and does not re-apply a boot configuration. Boot is not a record: boot configuration is a device setting.

## Remote (`sys_actions_router`, root key `ROUTER_KEY_ACTIONS` = 0x06)

All IDs are u32 LE. `info` = `{u16 bytes, u16 count, u32 crc32}`.

Requests are `[0x06][dir][op][args]` (`dir` 0 read, 1 write; ROUTER.MD). READ, INFO and LIST are reads, every other op is a write.

| Op | Request | Response |
|---|---|---|
| `0x01` RUN | `u32 id` | – (error record on a failing step) |
| `0x04` BEGIN | `u32 id, u16 len` | – |
| `0x05` WRITE | `u16 offset`, data | – (offset = bytes so far) |
| `0x06` COMMIT | `u32 crc32` | – (checks length, CRC and framing; stores, replacing the ID) |
| `0x07` READ | `u32 id, u16 offset` | the image from `offset`: as many bytes as are left, at most what one answer holds; empty at the end. Read again from `offset` + the bytes got. `ERR_ACTION_NOT_FOUND`; `ERR_ACTION_OFFSET {got, want}` for an offset past the image (`want` = its size) |
| `0x08` INFO | `u32 id` | info |
| `0x09` LIST | `u32 start` | `u16 total` (all stored records), then u32 IDs ascending from `start`, up to 48 and as many as fit. Ask again from the last ID + 1 |
| `0x0A` REMOVE | `u32 id` | – |
| `0x0B` ABORT | – | – (drops the record being written) |
| `0x0D` RUN_ALL | `u32 id` | – like RUN, but every frame runs. A failing frame is reported (error record, `ERR_ACTION_STEP`) and the run goes on; the first failure is the answer. Used to start a project, so one failing device leaves the rest configured. A record may contain RUN_ALL |
| `0x0E` REMOVE_ALL | – | – (every stored record, of every class: settings, devices, project and user. `ERR_ACTION_BUSY` while a record is being written. Stops at the first failing removal) |

`REMOVE` takes one ID. To clear one group only, list the IDs (`sys_actions_list`) and remove them one by one, as `sys_project`'s `clear_bank` does. REMOVE_ALL erases the stored records only. Devices and settings already running stay as they are until the board restarts; after a restart the board starts from its defaults, with no stored project and no device records.

READ is the mirror of the WRITEs: the image that went in comes out, by offset, page by page, as large as the link carries (BLE before the MTU exchange: small pages). A client reads from 0 up to INFO's `bytes`, checks the image against INFO's `crc32` and splits it into its frames (each exactly as written, from its key, with its dir). In C, `sys_actions_read(id, offset, buf, cap, &len)` is the same read, and `sys_actions_dump(id, fn, ctx)` walks a record frame by frame (`sys_project` reads its marks so).

**Examples**
- Save the log options as the system record: BEGIN `06 01 04 01 00 00 01 0A 00` (id `0x01000001`, 10 bytes), WRITE `06 01 05 00 00` (offset 0) and the 10-byte image `08 00 08 01 01 03 01 00 01 01` (one frame of 8 bytes: log, write, CONFIG, level 3, mirror on, trace off, errors on, logs on), COMMIT `06 01 06` with the CRC.

## Errors (`ERR_MOD_SYS_ACTIONS` = 8)

| Error | Severity |
|---|---|
| `ERR_ACTION_NOT_FOUND {id u32}` | MEDIUM |
| `ERR_ACTION_BAD_ID` (ID 0) | HIGH |
| `ERR_ACTION_BUSY` | MEDIUM |
| `ERR_ACTION_NO_DRAFT` | MEDIUM |
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

