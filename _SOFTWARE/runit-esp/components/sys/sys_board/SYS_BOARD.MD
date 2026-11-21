# sys_board — the board's profile

Component `runit-esp/components/sys/sys_board/`. What the board is, kept on the board: the frames that create its buses, ports and devices and set it up, and a JSON the app reads back. The app writes both and checks them; the firmware only runs the frames.

| File | Role |
|---|---|
| `sys_board.h` / `.c` | `sys_board_init` (the JSON file), `sys_board_boot` (the record's RUN_ALL) |
| `internal/sys_board_internal.h` | `sys_board_json`, for the router only |
| `router/sys_board_router.h` / `.c` | The remote interface (root key `MODULE_ID_BOARD` 0x27): the frames, then `BOARD_OPERATIONS`; registers itself |
| `router/contract_board.json` | The router's descriptor |

## Model

The profile is two stored things:

| | Frames | JSON |
|---|---|---|
| What | The record `SYS_ACTIONS_ID_BOARD` (class `0x04`, SYS_ACTIONS.MD): the power layout and rails, the I2C buses and the UART ports (a port's CREATE: the way to a slave, a device's port, a raw port), the providers of the board's ways to other boards, their sessions and their links (the session's `ATTACH`, the data matrix's `CONNECT`: SYS_SESSION.MD, DATA_MATRIX.MD; the board's own console and BLE are made by `app/runit`, and a record frame of the same role or route changes nothing), the `CREATE` of each device but the ESP32-S3's GPIO, and the board's settings | The app's descriptor of the board |
| Read by | The firmware, at boot | The app, never the firmware |

The app (`runit-app/src/app/devices/boardProfile.ts`) encodes its board descriptor into frames and compares them and the JSON with what is stored each time the board connects; a board without them gets them and restarts. The record is written like any record (sys_actions BEGIN, WRITE, COMMIT).

## Boot

`sys_board_boot` queues a RUN_ALL of the record (`router_inject`): a failing frame is reported and the next ones run. `app/runit` calls it once the router runs and the board's own devices exist (the ESP32-S3's GPIO, whose pins the record's buses and ports name). With no record the board has no devices of its own but its GPIO, and no way but its console and BLE: its ways to other boards (any number, of any kind) are the record's.

## A device of the board

The ESP32-S3's GPIO (device `0x00`) is the one device the record does not create: `app/runit` creates it before the record runs. The app sets the flag `SYS_DEVICE_CREATE_SYSTEM` in a device's `CREATE` in the record (SYS_DEVICE.MD): such a device can't be removed and the REMOVE sweep leaves it. It goes only when the app stores a record without it and the board restarts.

## The JSON

A JSON file of `nvs_store` (NVS_STORE.MD), namespace `board` in the partition `CONFIG_SYS_ACTIONS_PARTITION`, opened by `sys_board_init`. The firmware stores it and never reads it.

## Remote (`sys_board_router`, root key `MODULE_ID_BOARD` 0x27)

| Op | Side | Args | Answer |
|---|---|---|---|
| `0x01` JSON_APPEND | W | `sys_board_append_args_t` `{u32 offset}`, data | – (`ERR_NVS_OFFSET` unless the offset is the file's length) |
| `0x02` JSON_ERASE | W | – | – |
| `0x03` JSON_INFO | R | – | `nvs_store_json_info_t` `{u32 bytes, u32 crc32, u16 chunks}` |
| `0x04` JSON_READ | R | `sys_board_read_args_t` `{u16 chunk}` | the chunk's bytes (`ERR_NVS_NO_CHUNK` past the last) |

The app writes: JSON_ERASE, JSON_APPEND from offset 0, then JSON_INFO to compare the length and the CRC with what it sent. The module has no errors of its own: its failures are `nvs_store`'s and `sys_actions`'s.

## Open

- The app does not yet write the provider `CREATE`, the session's `ATTACH` and the data matrix's `CONNECT` frames of the ways to other boards.
- Not run on a board yet.
