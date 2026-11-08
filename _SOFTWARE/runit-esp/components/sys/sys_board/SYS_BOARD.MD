# sys_board — the board's profile

Component `components/sys/sys_board/`. What the board is, kept on the board: the frames that create its devices and set it up, and a JSON the app reads back. The app writes both and checks them; the firmware only replays the frames.

| File | Role |
|---|---|
| `sys_board.h` | API, the record's ID |
| `sys_board.c` | `sys_board_init`, `sys_board_boot`, the JSON file |
| `router/sys_board_router.c/.h` | `board` router (`ROUTER_KEY_BOARD` = 0x11), op IDs `SYS_BOARD_OP_*`. The header publishes every op (`SYS_BOARD_OP_*`, with its sides) and the wire layouts, each op above its frame |
| `router/contract_board.json` | Contract descriptor of the router |

## Model

The profile is two stored things:

| | Frames | JSON |
|---|---|---|
| What | The record `SYS_BOARD_RECORD_ID` (class `0x04`, SYS_ACTIONS.MD): the power layout and rails, the `CREATE` of each device but the ESP32-S3's GPIO, the I2C buses, the set-up of the pins | The definition of the devices in the format of `board.json`, names included |
| Read by | The firmware, at boot | The app, never the firmware |

The app (`runit-app/src/app/devices/boardProfile.ts`) encodes its board descriptor into frames and compares them and the JSON with what is stored, each time the board connects; a board without them gets them and restarts. The board always runs the record.

## Boot

The boot step `board at start`, `sys_board_boot`, queues a RUN_ALL of the record with `router_inject`: a failing frame is reported and the next ones run. With no record the board has no devices and no settings of its own but the ESP32-S3's GPIO (below): the UART console and BLE, enough for the app to write a profile.

## A device of the board

The ESP32-S3's GPIO (device 0, `RUNIT_BOARD_GPIO_DEVICE_ID`) is the one device the record does not create. Every build has it and has only one, and the record's buses, modes and levels name its pins, so the boot step `esp32 gpio` (`runit_board_gpio_init`, RUNIT.MD) makes it, with the flag below and no pin set up, before the record runs. The app leaves its `CREATE` out of the frames (`SINGLE_TYPES`).

The app sets the flag `SYS_DEVICE_CREATE_SYSTEM` in a device's `CREATE` in the record (SYS_DEVICE.MD). Such a device can't be removed (`ERR_DEV_SYSTEM`) and the REMOVE sweep, so a project's ERASE, leaves it. It goes only when the app stores a record without it and restarts the board.

## The JSON

A file of `sys_nvs` (SYS_NVS.MD), namespace `board` in the `project` partition, opened by the boot step `board`. The firmware stores it and never reads it.

## Remote

| Op | Side | Args | Answer |
|---|---|---|---|
| `0x01` JSON_APPEND | W | `u32 offset`, data | – (`ERR_NVS_OFFSET` unless the offset is the file's length) |
| `0x02` JSON_ERASE | W | – | – |
| `0x03` JSON_INFO | R | – | `u32 bytes, u32 crc32, u16 chunks` |
| `0x04` JSON_READ | R | `u16 chunk` | the chunk's bytes (`ERR_NVS_NO_CHUNK` past the last) |

The app writes: JSON_ERASE, JSON_APPEND from offset 0, then JSON_INFO to compare the length and the CRC with what it sent.

## Open

- `SYS_ACTIONS_CLASS_BOARD` in `sys_actions.h`, and `SYS_BOARD_ACTIONS_CLASS` taken from it. Writing this class only followed by a restart.
- The link's pins and rate still come from Kconfig: an op for them, so they become frames of the record. (Its role is the sessions' already: SYS_SESSION.MD "Boards, clients, the star".)
- A board with a master takes no client (SYS_SESSION.MD): it must, for the console and BLE to stay.
- The record's size: a limit of `CONFIG_SYS_ACTIONS_MAX_BYTES`, the PCB's frames not measured yet.
