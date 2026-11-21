# sys_actions — stored command lists

Component `runit-esp/components/sys/sys_actions/`. One store for everything the board keeps as commands. A **record** is a list of router frames stored in NVS under a 32-bit **ID**, replayed through `router_route`. A client writes a record whole (BEGIN, WRITE, COMMIT) and reads it back by offset, the image exactly as stored.

The store holds the kinds of record told apart by the class in the ID: the user's **actions**, the **settings** of the system modules (log, power, BLE, data streams), the **install and default commands** of each device, the **project**, and the **board's profile**.

**Nothing replays by itself.** A client, the VM or a module calls RUN. After a reset the board is in its default state (module defaults, devices off); a stored record changes nothing until it is run.

## Files

| File | Role |
|---|---|
| `sys_actions.h` | API, ID classes, system IDs, onboard actions |
| `sys_actions.c` | The NVS store, run, writing a record, the onboard actions; `ERR_MODULE_DEFINE` |
| `sys_actions_id.h`, `errors/sys_actions_errors.h` | `MODULE_ID_DECLARE(MODULE_ID_ACTIONS, 0x28)` (`sys_actions_id.h`), `ERR_SYS_ACTIONS_MAP`; `errors_sys_actions.json` the messages |
| `router/sys_actions_router.h` / `.c` | The remote interface (root key `MODULE_ID_ACTIONS` 0x28): the frames, then `ACTIONS_OPERATIONS`; registers itself |
| `router/contract_actions.json` | The router's descriptor |
| `Kconfig` | Partition, largest record, nesting depth |

## Model

**ID = identity.** A u32 `[class u8][index u24]`; 0 = none (`ERR_ACTION_BAD_ID`). The client chooses it, except the system IDs, which are fixed in `sys_actions.h`.

| Class | `SYS_ACTIONS_CLASS_*` | Holds |
|---|---|---|
| `0x00` | USER | the app's actions (index 1..; 3 to 6 are the onboard actions) |
| `0x01` | SYSTEM | settings of the system modules, one record each (below) |
| `0x02` | DEVICE | `0x02000000 \| device ID` (`SYS_ACTIONS_ID_DEVICE(dev)`): install (CREATE) and default commands |
| `0x03` | PROJECT | `0x03000000` the project record; the other IDs of this class are the project module's |
| `0x04` | BOARD | `0x04000000` `SYS_ACTIONS_ID_BOARD`: the board's profile, run at boot (SYS_BOARD.MD) |

System IDs (class `0x01`):

| ID | Record | Set by |
|---|---|---|
| `0x01000001` `SYS_ACTIONS_ID_LOG` | log options (the CONFIG write) | log |
| `0x01000002` `SYS_ACTIONS_ID_POWER_CFG` | power CONFIG | power |
| `0x01000003` `SYS_ACTIONS_ID_POWER_PSU` | power PSU | power |
| `0x01000004` `SYS_ACTIONS_ID_POWER_BATTERY` | power BATTERY | power |
| `0x01000005` `SYS_ACTIONS_ID_POWER_RESPONSES` | power MATRIX (the response matrix) | power |
| `0x01000006` `SYS_ACTIONS_ID_BLE` | Bluetooth: the BLE providers (characteristics) the app adds | the app |
| `0x01000007` `SYS_ACTIONS_ID_DATA_MATRIX` | provider roles and routes of the data | data_matrix |
| `0x01000008` `SYS_ACTIONS_ID_POWER_PD` | power PD (the USB-PD request) | power |
| `0x01000009` `SYS_ACTIONS_ID_LINKS` | the links to other boards: BLE CONNECT, the session's ATTACH and the data matrix's CONNECTs per board | the app's Boards page |

Class `0xFF` holds no records: an error policy's actions use it for the policy's own responses (`sys_device_policy.h`). Any client may write any other class and RUN any class.

**Settings are records.** A settings record is the module's own write frame (`[root key][0x01 write][op][args]`) saved by the app, e.g. the log options as `[0x29][0x01][0x01][level, mirror_serial, trace_errors]` (log CONFIG). The write applies the value live and stores nothing; persistence is the app writing the record.

**Image.** Frames back to back, each `[u16 len LE][key][dir][…]`: what `router_route` takes (no seq, no flags; the dir stays), 1..`CMD_DATA_MAX` bytes. The total is at most `CONFIG_SYS_ACTIONS_MAX_BYTES` (4096), of every class; a longer image is `ERR_ACTION_TOO_BIG {len, max}` (BEGIN, `sys_actions_set`). The CRC (`crc32_ieee(0, …)`, zlib / IEEE, CRC.MD) covers the image, which is also what a client sends.

**Storage.** Namespace `actions` in the partition `CONFIG_SYS_ACTIONS_PARTITION` (`nvs`, made ready by `nvs_store_init` in `app/runit`), key `a<id>` (the ID as 8 hex digits, e.g. `a02000063`), one value per record, through `nvs_store` (NVS_STORE.MD). One `nvs_store_set` per store; the store keeps the old value until the new one is complete. The store checks framing only, not content: a frame the router can't take fails at its step.

**One draft at a time.** A draft is a record being written: the image on the heap, from BEGIN to COMMIT or ABORT. Another BEGIN → `ERR_ACTION_BUSY`. COMMIT closes the draft even when it fails.

## Running

`sys_actions_run(id, keep_going)` loads the image and routes every frame in order, each with its own dir. Answers are dropped. A failing frame's error is wrapped in `ERR_ACTION_STEP {id u32, step}` (step counted from 0), so the report shows the action, the step, the route and the cause.

- **Only on a router task** (`router_origin() != NULL`, else `ERR_ACTION_WRONG_TASK`): a remote RUN, or a RUN queued with `router_inject`. `sys_actions_request(id, urgent)` queues it for callers that aren't a router task (the VM, the error policy's runner). A replay never interleaves with live normal commands, since they wait on the same task; an urgent STOP still overtakes it.
- **RUN stops at the first failing frame, RUN_ALL does not** (`keep_going`): it runs every frame, reports each failure, and returns the first. A RUN_ALL inside a record continues inside it, but a failure it returns still stops an outer plain RUN.
- **Nesting.** A RUN inside a record nests up to `CONFIG_SYS_ACTIONS_MAX_DEPTH` (3, counted per router task); deeper → `ERR_ACTION_DEPTH`. A record that runs itself stops there. So a program can be a record of RUN frames: system records first, then devices, then code; the firmware sorts nothing.
- Each nesting level holds its image plus one router frame of heap while it runs.

## Set from the firmware

`sys_actions_set(id, image, len)` stores a whole image without a draft, so it works while a client writes another record. The framing is checked like a commit's. It writes nothing when the same image is already stored: no flash wear on every boot.

`sys_actions_copy(from, to)` copies a record; `sys_actions_info` returns `{bytes, count, crc32}`; `sys_actions_read` is the READ in C; `sys_actions_dump(id, fn, ctx)` walks a record frame by frame; `sys_actions_list` and `sys_actions_remove` serve a module that keeps a group of records (the project).

## Onboard actions

The firmware's own actions, written by `sys_actions_init` at every boot with `sys_actions_set` (only when they differ, so a client's edit of these IDs is replaced). Their IDs are fixed, `SYS_ACTIONS_ONBOARD_*` in `sys_actions.h`, published as the group `onboard_action`, so the app keeps its own actions off them. Each is one write frame: a device-router sweep to `SYS_DEVICE_ID_NONE` (`sys_device_router.h`: `[0x20][dir][id][op u16]`), or a sys command.

| ID | Does | Frame |
|---|---|---|
| 3 `SYS_ACTIONS_ONBOARD_RESUME` (`0x00000003`) | resume all | `2F 01 FF 03 2F` (CONTRACT_SYS_DEVICE RESUME, every device) |
| 4 `SYS_ACTIONS_ONBOARD_SUSPEND` (`0x00000004`) | suspend all | `2F 01 FF 02 2F` |
| 5 `SYS_ACTIONS_ONBOARD_RESET` (`0x00000005`) | reset all devices | `2F 01 FF 01 2F` (the drivers' RESET) |
| 6 `SYS_ACTIONS_ONBOARD_HARD_RESET` (`0x00000006`) | hard reset: restart the board | `26 01 03` (sys RESTART, ~100 ms after the answer) |

Run with `2C 01 01 <id u32>` (actions RUN, a write), or name the ID in a device's error policy. Hard reset restarts the board: the devices and the links are rebuilt by the boot. Reset all touches the devices only.

## Remote (`sys_actions_router`, root key `MODULE_ID_ACTIONS` 0x28)

All IDs are u32 LE. `info` = `sys_actions_info_t` `{u16 bytes, u16 count, u32 crc32}`. Requests are `[0x28][dir][op][args]`; READ, INFO and LIST are reads, every other op is a write.

| Op | Request | Response |
|---|---|---|
| `0x01` RUN | `u32 id` | – (error record on a failing step) |
| `0x04` BEGIN | `u32 id, u16 len` | – |
| `0x05` WRITE | `u16 offset`, data | – (offset = bytes so far) |
| `0x06` COMMIT | `u32 crc32` | – (checks length, CRC and framing; stores, replacing the ID) |
| `0x07` READ | `u32 id, u16 offset` | the image from `offset`: as many bytes as are left, at most what one answer holds; empty at the end. `ERR_ACTION_NOT_FOUND`; `ERR_ACTION_OFFSET {got, want}` past the image |
| `0x08` INFO | `u32 id` | info |
| `0x09` LIST | `u32 start` | `u16 total` (all stored records), then u32 IDs ascending from `start`, up to 48 and as many as fit. Ask again from the last ID + 1 |
| `0x0A` REMOVE | `u32 id` | – |
| `0x0B` ABORT | – | – (drops the record being written) |
| `0x0D` RUN_ALL | `u32 id` | – like RUN, but every frame runs; the first failure is the answer |
| `0x0E` REMOVE_ALL | – | – (every stored record, of every class; `ERR_ACTION_BUSY` while a record is being written; stops at the first failing removal) |

0x02, 0x03 and 0x0C are unused: never assign them. READ is the mirror of the WRITEs: a client reads from 0 up to INFO's `bytes`, checks the image against INFO's `crc32` and splits it into its frames.

## Errors (`MODULE_ID_ACTIONS` 0x28)

`ERR_ACTION_NOT_FOUND {id}`, `_BAD_ID`, `_BUSY`, `_NO_DRAFT`, `_TOO_BIG {len, max}`, `_OFFSET {got, want}`, `_INCOMPLETE {got, want}`, `_CRC {got, want}` (got = the client's), `_BAD_FRAME {offset}`, `_EMPTY`, `_DEPTH {depth}`, `_WRONG_TASK`, `_NO_MEM {size}`, `_STEP {id, step}` (context), `_BAD_ARG`. NVS failures come as `ERR_ESP_ERR {code}`.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_ACTIONS_PARTITION` | `nvs` | the NVS partition of the records |
| `CONFIG_SYS_ACTIONS_MAX_BYTES` | 4096 | the largest image, of every class |
| `CONFIG_SYS_ACTIONS_MAX_DEPTH` | 3 | RUNs nested in records |

## Open

- The records share the `nvs` partition (24 KB of the default single-app table): a `project` partition of its own joins with the board's partition table.
- Its users: the board's profile (`sys_board`, its record run at boot); later the project (store, copy, list, nested RUNs), the error policy's runner and the VM (`sys_actions_request`).
- Not run on a board yet.
