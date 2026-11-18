# runit-esp-rework — the firmware rebuilt component by component

An ESP-IDF project parallel to `runit-esp/`, with its own `sdkconfig` and `build/`, for the reworked `sys`: only the new components live here, in `components/` grouped by layer like the firmware (`core`, `sys`, `sys/bus`, `devices/{soc,chips,virtual}`). What is not rewritten yet is taken from `runit-esp/components/` by folder (the list is at the top of `CMakeLists.txt`). A step moves a component here (or adapts it until it builds) and adds it to the build; the old firmware keeps its own versions until the rework replaces it.

Build: `.\build.ps1` (activates ESP-IDF, builds, prints the errors, warnings and the result; `.\build.ps1 reconfigure` after a folder joins or a Kconfig changes). The first build compiles all of ESP-IDF (~1100 steps), later ones only what changed.

## In the build

| Layer | Component | Where |
|---|---|---|
| core | `time` | shared: `runit-esp/components/core/` |
| core | `utils` (`utils.h`, `module_id.h`), `errors`, `cmd` (`cmd.h`, `cmd_table.h`, `ERR_CMD_*`), `router` (module op tables, `router_table.h`, routing), `router_queue` (the queue and the two tasks), `timer` (the board's timers) | here |
| sys | `sys_event` | here |
| sys | `sys_device` (the registry) and its satellites `sys_device_event`, `sys_device_timer`, `sys_device_router`, `sys_device_policy` | here |
| sys | the contracts `sys_io` (`sys_io.h` + `sys_io_driver.h`), `sys_power`, `sys_hbridge`, `bus/sys_i2c` (a bus is a device: the contract and its client side) | here |
| sys | `sys_power_manager` (its POWER_VREG budget is an interceptor of the registry) | here |
| sys | `sys_nvs` (the NVS partitions, JSON files in chunks), `sys_data_provider` (minimal typed provider registry: `CREATE`/`REMOVE`/`LIST`/`SEND`/`BIND_COMMANDS`/`UNBIND_COMMANDS`/`INFO`/`OPS` + `0xFFxx` own ops, `[dir][id][op u16]` router on `MODULE_ID_PROVIDER` `0x0D`, `sys_data_provider_type_e` `u8`): rebuilt | here |
| devices/soc | `device_esp32s3_gpio` (the ESP32-S3's own pins: digital, PWM, ADC, pin events), `device_esp32s3_i2c` (the ESP32-S3's I2C controller as a bus device), `device_esp32s3_uart` (ESP32-S3 UART controllers as `CONTRACT_UART` devices made by CREATE with any configuration, on the heap: `FRAMED`/`RAW`/`RAW_LINE`, ISR RX events, one reader) | here (`runit_device`) |
| devices/chips | `device_pca9685`, `device_tca6424a`, `device_tps55289` | here (`runit_device`) |
| devices/virtual | `device_dc_motor` | here (`runit_device`) |
| providers | `provider_uart` (`SYS_DATA_PROVIDER_TYPE_UART = 1`: self-registering provider driver bridging any `CONTRACT_UART` device via `OP_INFO`, `OP_RX_HOOK`, and `OP_SEND_FRAME`, the CREATE cfg as its only header/router), `provider_console` (`SYS_DATA_PROVIDER_TYPE_CONSOLE = 4`: the board's UART0 + USB-Serial-JTAG console, COBS + CRC16, no public header), `provider_ble` (`SYS_DATA_PROVIDER_TYPE_BLE = 2`: a provider of one or two characteristics of `sys_ble`, cfg `{u16 svc, u16 tx, u16 rx, u8 flags, char name[16]}`: it makes its service and characteristics, no public header) | here (`runit_provider`) |
| sys | `bus/sys_uart_contract` (the UART contract, a port is a device addressed by `device_id` only and isolated from `sys_data_provider`: INFO, BAUD, SEND with NORMAL/URGENT/SPARE flags, RECEIVE, FLUSH, SUBSCRIBE, TRANSACT, plus LOCAL SEND_FRAME and RX_HOOK for external provider adapters, and `UART_EV_OVERFLOW` event; modes/parity/limits in `sys_uart_contract.h`; the line's framing and TX queue for the ports and the console in `sys_uart_codec.h`, the errors and ID of `MODULE_ID_UART` 0x13): implemented by `device_esp32s3_uart` and bridged by `provider_uart` (stage 3a) | here |
| sys | `sys_actions` (stored command lists: records in NVS under u32 IDs with classes, upload / readback / RUN / RUN_ALL through the router, the onboard actions; `MODULE_ID_ACTIONS` `0x2C`, partition `nvs` for now) | here |
| sys | `bus/sys_ble` (the BLE peripheral: NimBLE, one client, no pairing; the run-time GATT table, staged and applied on the BLE task; the table's calls for the providers and `sys_ble_start`, a router that only reads the table and the state; the framing both ways; one hook per characteristic for its writes and the link's state; `MODULE_ID_BLE` `0x2E`): bridged by `provider_ble` | here |
| sys | `sys_board` (the board's profile: the record `SYS_ACTIONS_ID_BOARD` run at boot as RUN_ALL, the JSON the app reads back; `MODULE_ID_BOARD` `0x2D`) | here |
| sys | `sys_session` (a session is a framed provider with one peer, no routing; the hello, a dead-man of its own, broadcast on every session, calls with a wait of their own limit, a PUSH `seq` per session and class), `sys_system` (system commands under `MODULE_ID_SYS` 0x28, the safe state as the registry's sweep, the dead-man): the third stage | here |
| sys | `sys_event_stream` (the events as two streams of the matrix: the notify events out on `0x06`, the events of other boards in on `0x07`; apart from `sys_event`, which `sys_session` needs), `data_matrix` (the roles of the providers, raw / session / command port, and the routes between ends: the one place that says where every byte that is not a command goes; the streams' sends, a module's streams in its own table; `MODULE_ID_DATA_MATRIX` `0x30`), `sys_log` (`ESP_LOG` hook, error record history, `MODULE_ID_LOG` `0x2A`): the fifth stage | here |
| sys | `sys_watch` (server PUSH `[SYS_WATCH_CHANNEL][id][status][data]` without `owner`, `board_timer` tick, triggers PERIODIC / ON_CHANGE; cache-only client `FREE`/`ADDING`/`ACTIVE` with B3 ADD-reply gate, a write adds the watch again, `MODULE_ID_WATCH` `0x2B`): the seventh stage | here |
| sys | `sys_remote` (devices of other boards: the registry's remote hook, WIRED calls waited for, WIRELESS ones on `sys_watch_read`, subscriptions to a device's events and errors on `SYS_REMOTE_CHANNEL`, no table of aliases (the registry's `sys_device_find_remote` / `sys_device_remote_of`), `MODULE_ID_REMOTE` `0x2F`; the policy's walk ends at `ERR_REMOTE`; `sys_session_wired`): the eighth stage | here |
| app | `app/runit` (the composition: `runit_start` runs the boot in its order; it starts the data matrix, starts `sys_log` (`LOGS`, `ERRORS`) and `sys_event_stream` (the notify events out, the events of other boards in), and the board's ways, all in the tables applied in one pass: the console and the runIT BLE pair with the role session and the routes of `ERRORS` and `EVENTS` to them, the logs' own BLE characteristic with the role raw and the route of `LOGS` to it (the ways to other boards are the board's record's, not made here); the error sink (policy, `sys_log_report`, `sys_remote_on_error`); the system `ESP32S3_GPIO` (`0x00`); the board's profile last) | here |
| main | `app_main` calls `runit_start` | here |

Every module has one number, `MODULE_ID_<NAME>` (`core/utils/module_id.h`): its router key, the module of its errors and of its events, and a contract's ID. A module writes it once, `MODULE_ID_DECLARE(MODULE_ID_X, 0x..)` in its own `<module>_id.h` (the component's folder, or `contracts/` for a power contract), and every header and macro takes it by that name (`MODULE_ID_CORE` is `0`, in `errors.h`).

`PROPOSAL.md` is the review of the registry (`sys_device`): the reasons for its design, what is open and in which order, with a list of what to test on the board. `PROPOSAL_NETWORK.md` is the review of the network part still in `runit-esp/` (sessions, watches, remote devices, streams, providers): its defects, the simpler version to build here and the stages that move it here (its section 6: what each stage copies, what changes at the copy, the test that closes it). `PROPOSAL_DATA_MATRIX.md` is the plan of `data_matrix` and what is left of it (the board's record frames, the master's `CALL`, the app), `components/sys/data_matrix/DATA_MATRIX.MD` what is built.

## Next

1. the other devices, one by one (`device_ina3221`, `device_drv8962`, ...)
2. the rest of `sys` with minimal changes
3. the descriptors of the moved components (`device_*.json`, `contract_*.json`) and `templates/sync.sh`, which still look in `runit-esp/`
