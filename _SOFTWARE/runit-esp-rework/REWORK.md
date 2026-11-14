# runit-esp-rework — the firmware rebuilt component by component

An ESP-IDF project parallel to `runit-esp/`, with its own `sdkconfig` and `build/`, for the reworked `sys`: only the new components live here, in `components/` grouped by layer like the firmware (`core`, `sys`, `sys/bus`, `devices/{soc,chips,virtual}`). What is not rewritten yet is taken from `runit-esp/components/` by folder (the list is at the top of `CMakeLists.txt`). A step moves a component here (or adapts it until it builds) and adds it to the build; the old firmware keeps its own versions until the rework replaces it.

Build: `.\build.ps1` (activates ESP-IDF, builds, prints the errors, warnings and the result; `.\build.ps1 reconfigure` after a folder joins or a Kconfig changes). The first build compiles all of ESP-IDF (~1100 steps), later ones only what changed.

## In the build

| Layer | Component | Where |
|---|---|---|
| core | `utils`, `time` | shared: `runit-esp/components/core/` |
| core | `errors`, `cmd` (`cmd.h`, `cmd_table.h`, `ERR_CMD_*`), `router` (module op tables, `router_table.h`, routing), `router_queue` (the queue and the two tasks), `timer` (the board's timers) | here |
| sys | `sys_event` | here |
| sys | `sys_device` (the registry) and its satellites `sys_device_event`, `sys_device_timer`, `sys_device_router`, `sys_device_policy` | here |
| sys | the contracts `sys_io` (`sys_io.h` + `sys_io_driver.h`), `sys_power`, `sys_hbridge`, `bus/sys_i2c` (a bus is a device: the contract and its client side) | here |
| sys | `sys_power_manager` (its POWER_VREG budget is an interceptor of the registry) | here |
| sys | `sys_nvs` (the NVS partitions, JSON files in chunks), `sys_data_provider` (providers: the send in two parts, `SPARE`, the command binding, its router `MODULE_ID_PROVIDER` 0x0D): the first stage of the network part (`PROPOSAL_NETWORK.md` section 6) | here |
| devices/soc | `device_esp32s3_gpio` (the ESP32-S3's own pins: digital, PWM, ADC, pin events), `device_esp32s3_i2c` (the ESP32-S3's I2C controller as a bus device) | here (`runit_device`) |
| devices/chips | `device_pca9685`, `device_tca6424a`, `device_tps55289` | here (`runit_device`) |
| devices/virtual | `device_dc_motor` | here (`runit_device`) |
| sys | `bus/sys_uart` (the board's UART ports as providers: the console and the link, the line COBS + CRC16, the link lent as a DIRECT port): the second stage | here |
| sys | `bus/sys_uart_contract` (the UART contract, a port is a device: INFO, BAUD, SEND, RECEIVE, FLUSH, SUBSCRIBE, LEND, WRITE, READ; its client; ID `MODULE_ID_UART` 0x13, claimed by `sys_uart`): written, no port implements it yet (stage 3a) | here |
| sys | `sys_session` (the board's ways: UP / DOWN, the hello, PEER, forwarding, calls with a wait of their own limit, the PUSH counters per peer and class), `sys_system` (system commands under `MODULE_ID_SYS` 0x28, the safe state as the registry's sweep, the dead-man): the third stage | here |
| sys | `sys_stream` (named streams, no `dir` — ruled by `SOURCE` presence, no per-stream seq — `sys_session` PUSH counters, 4 target types: `SOURCE`/`SINK`/`PEER`/`CALLBACK`, `MODULE_ID_STREAM` `0x29`), `sys_log` (`ESP_LOG` hook, error record history, `MODULE_ID_LOG` `0x2A`): the fifth stage | here |
| sys | `sys_watch` (server PUSH `[0x80][id][status][data]` without `owner`, `board_timer` tick, `sys_event_key_t` filter; cache-only client `FREE`/`ADDING`/`ACTIVE`/`OLD` with B3 ADD-reply gate, `MODULE_ID_WATCH` `0x2B`): the seventh stage | here |
| main | logs the uptime, starts NVS, router, event bus, device policy, sessions, system streams (`TELEMETRY`, `LOGS`, `ERRORS`, `EVENTS`) + `sys_log` + error sink + notify-event forwarding, system commands, watches and UART ports, attaches console `UP` and link `DOWN` | here |

Every module has one number (`core/utils/module_id.h` in `runit-esp`): its router key, the module of its errors and of its events, and a contract's ID. All modules here declare theirs via `MODULE_ID_DECLARE` (`ERR_MOD_CORE` is `0` in `errors.h`).

`PROPOSAL.md` is the review of the registry (`sys_device`): the reasons for its design, what is open and in which order, with a list of what to test on the board. `PROPOSAL_NETWORK.md` is the review of the network part still in `runit-esp/` (sessions, watches, remote devices, streams, providers): its defects, the simpler version to build here and the stages that move it here (its section 6: what each stage copies, what changes at the copy, the test that closes it).

## Next

1. the other devices, one by one (`device_ina3221`, `device_drv8962`, ...)
2. the rest of `sys` with minimal changes
3. the descriptors of the moved components (`device_*.json`, `contract_*.json`) and `templates/sync.sh`, which still look in `runit-esp/`
