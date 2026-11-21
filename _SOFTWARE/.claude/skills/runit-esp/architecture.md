# Firmware architecture

How the components fit together. Each component's own `*.MD` holds its API and wire format; this file only links them. Paths are under `runit-esp/components/` (the parts not moved yet: `runit-esp-archive/components/`, SKILL.md "Component docs").

## 1. The idea

A plug-and-play board for hobbyists. The board exposes what it has (devices, contracts, records) as **commands** on one router; a client (the app, a test host, the board itself) builds and drives everything through them. Everything the board says comes back as **streams**: PUSH packets out of the module's port of the data matrix. A user program is compiled off-device, uploaded as VM records, stored as a project and run by the VM (the VM and the project store are not moved into the rework yet). Nothing on the board decides on its own beyond safety: the device error policy and the power manager.

## 2. Layers

| Layer | Components | May depend on |
|---|---|---|
| core | `utils`, `errors`, `cmd`, `router`, `router_queue`, `data_matrix`, `timer`, `crc`, `nvs_store`, `board_id` (`time` is still the archive's) | ESP-IDF only (and earlier core) |
| sys | `sys_event`, `sys_event_stream`, `sys_device` (+ `sys_device_event`, `_timer`, `_router`, `_policy`, `_remote`), the contracts `sys_io`, `sys_power`, `sys_hbridge`, `bus/sys_i2c`, `bus/sys_uart_contract`, `bus/sys_ble`, `sys_data_provider`, `sys_session`, `sys_watch`, `sys_log`, `sys_system`, `sys_actions`, `sys_board`, `sys_power_manager` (`sys_lvgl`: not built yet) | core, earlier sys |
| providers | `provider_uart`, `provider_console`, `provider_ble`: drivers of `sys_data_provider` | core, sys |
| devices | `devices/{soc,chips,virtual}/device_<chip>`, one driver each | core, sys (the contracts they implement) |
| vm | `vm_core`, `vm_blocks` (not moved yet) | core, sys, devices |
| app | `app/runit` (boot, wiring, the board's data), `main` (only `runit_start()`) | everything |

Rules:
- A lower layer never includes a higher one. Where it must call up, the higher layer **registers** a function or a port at boot (§5), from `runit`.
- Self-registration by constructor exists only for tables of types and the routers: device types (`SYS_DEVICE_TYPE_DEFINE`), provider types (`SYS_DATA_PROVIDER_TYPE_DEFINE`), module routers (`ROUTER_MODULE_REGISTER`), error tag maps (`ERR_MODULE_DEFINE`). Those components are linked `WHOLE_ARCHIVE`.
- Declare only real dependencies in CMake (`REQUIRES` / `PRIV_REQUIRES`).

## 3. Boot

`runit_start()` runs the boot steps in order (RUNIT.MD "Boot order"): `nvs_store_init`, `board_id_init`, `sys_actions_init`, `sys_board_init`, `router_init`, `sys_log_init`, `runit_data_matrix_setup` (the providers, the sessions and their links, BLE), the error sink, the ESP32-S3's GPIO, `sys_board_boot` (the board's profile). A failing step is reported and the boot goes on.

## 4. Flows

- **Inbound command:** a provider's driver (`provider_console`: COBS + CRC16 frames on the console; `provider_ble`: the runIT RX characteristic) sends a whole frame out of its port; the session's port (SYS_SESSION.MD "The ports") takes it: a REQUEST `[type][src][key][seq u16][dir][code]...[payload]` (CMD.MD "The header") goes out of its sub-port `MODULE_ID_ROUTER` to the router's port (ROUTER_QUEUE.MD) → the urgent or work task → root key → the module's router → its API → the answer `[ANSWER, status][key][seq][data]` sent by name to the port it came from, which puts this board's id as `src` and sends it onto the line. The board's own commands: `router_inject` (src 0xFE); its calls to another board: `sys_session_request` (`router_request` to that board's session, `sys_session_port`). `dir` picks the command's read or write side; a command with both is a mirror (the read answers what the write takes, CMD.MD "Read and write").
- **The data matrix** (DATA_MATRIX.MD) is the one place bytes are routed: every provider, session and stream module is a port, and what is connected is connected by links (the `CONNECT` command, the board's record); a sender never names a target but for an answer (`data_matrix_send_to`).
- **Streams:** a stream is a module's port named by its module number, each packet `[hdr PUSH, key][data]`: `LOGS` (the log lines as text, out) and `ERRORS` (this board's records out, other boards' in and passed on, SYS_LOG.MD), `EVENTS` (the notify events out, other boards' in: published without NOTIFY and passed on, SYS_EVENT_STREAM.MD), `WATCH` (0x2A), `REMOTE` (0x31). Where a stream goes is its links: to a session's sub-port of that module (a PUSH to its peer), to a raw provider (the bytes as they are: the BLE logs and telemetry), from another board's session's sub-port in. A port that passes on what it takes sends it everywhere but back where it came from (`data_matrix_send_except`).
- **Providers:** one way bytes go out and come in (a UART device: the link to a slave; the console; the runIT BLE pair; a user BLE characteristic), made by `sys_data_provider` CREATE. Each provider is a port: what comes in on its line goes out of its port to the ports linked from it, what its port takes is sent on its line (SYS_DATA_PROVIDER.MD "Stream port").
- **Sessions and the dead-man:** a session is the peer of a framed provider (a board or a client), known by its hello (a board its place in the project, a client one of 0xF0..0xFC; two open sessions with one id: `ERR_SESSION_ID_TAKEN`); it opens when both hellos have passed and is kept with PINGs. A session that ends (silence, its provider down) is told by `SESSION_EV_GONE`: the watches and the remote devices drop what they kept for it, and a session with the safety flag enters the safe state (SYS_SYSTEM.MD).
- **Errors:** a chain (ERRORS.MD) is returned up to whoever ends it; `err_report` → the sink `error_sink` in `runit.c`: the device error policy (`sys_device_policy_handle`, SYS_DEVICE_POLICY.MD; it answers in a task of its own) → `sys_log_report` (the history and the port `ERRORS`) → `sys_device_remote_on_error`. Errors of a routed command are answered and reported with that command.
- **Events:** a source publishes (`sys_event_publish`, SYS_EVENT.MD): the listener of `sys_event_stream` (`SYS_EVENT_RUN_FIRST`) sends NOTIFY events out of `EVENTS` first, then the inline listeners, then the queued ones in the task of the event's lane. A device's suspended flag changing or a RESET that worked is `sys_device`'s own event (SUSPENDED / RESUMED / RESET, one for a sweep).
- **Other boards:** another board is the peer of a session over a framed provider (a UART link; the board's record makes the provider, the session's `ATTACH` and the data matrix `CONNECT`s, SYS_BOARD.MD). Nothing relays: a frame goes where its session's links go; a board that wants another board's stream links it (its `EVENTS` and `ERRORS` pass on what they take), a board's call to another is `sys_session_request`.
- **Watches:** a peer's ADD (`MODULE_ID_WATCH`) names a read, a trigger and a period; `sys_watch` injects that read as a command of the board on its tick and sends each answer as an entry `[hdr PUSH, MODULE_ID_WATCH][owner][id][status][data]` out of its port `WATCH` to its links (a session's sub-port, the raw BLE telemetry; a taker keeps its own by `owner`). The other way, this board's reads of another board (`sys_watch_read`, `sys_watch_stale`) keep a cached watch there (SYS_WATCH.MD "Cached reads").
- **Devices:** `[MODULE_ID_SYS_DEVICE][dir u8][id u8][op u16][args]` → `sys_device_router` → `sys_device_call_routed` → the driver's op table; contract ops are `0xCCxx` (contract ID = its module's number, op), a driver's own ops `0xFFxx` (SYS_DEVICE.MD, SYS_IO.MD, SYS_POWER.MD, SYS_HBRIDGE.MD). An op flagged `SYS_DEVICE_OP_LOCAL` runs only from C; the router and a remote device refuse it (`ERR_DEV_LOCAL_OP`). A device of another board is a **remote device** (`CREATE_REMOTE`), a local alias: the registry keeps its type and where it is, and `sys_device_call` hands the call to `sys_device_remote`'s hook, which sends that board's device command with the id swapped: waited for on a wired neighbour (`WIRED`) or for a `DIRECT` read, else a write not waited for and a read from the copy a watch on that board keeps. Its events and errors come by a subscription to the device on its board (the port `REMOTE`, 0x31) and are published and reported as the local id's (SYS_DEVICE_REMOTE.MD).
- **Records:** any command frame can be stored in a record (SYS_ACTIONS.MD) and run later; the board's profile is one (`SYS_ACTIONS_ID_BOARD`, run at boot, SYS_BOARD.MD). Settings are records of a module's write frame; nothing else replays by itself.

## 5. Wiring (all in `app/runit`)

| What | Where |
|---|---|
| Module routers under their root keys | each module's `ROUTER_MODULE_REGISTER`, bound by `router_init` |
| The modules' ports | registered by `router_init` and `sys_log_init`, the others by a constructor of their module (before `app_main`) |
| The board's providers, sessions and links (console, runIT BLE pair, raw logs and telemetry) | `connectivity/data_matrix_setup.c` (`k_providers`, `k_sessions`, `runit_data_matrix_setup`) |
| The error sink | `error_sink` in `runit.c` |
| Power layout and limits for the power manager | the board's profile (`SYS_BOARD.MD`, LAYOUT and RAIL of `SYS_POWER_MANAGER.MD`); the manager is not started in `runit_start` yet |
| Onboard devices, their IDs and the I2C buses and UART ports | the ESP32-S3's GPIO in `runit.c`; the rest the board's profile (`SYS_BOARD.MD`), `runit-app/boards/` |
| The firmware's own actions (resume / suspend / reset all, hard reset) | `k_onboard` in `sys_actions.c` (`SYS_ACTIONS_ONBOARD_*`) |
| Safe state: every device suspended | `sys_system_enter_safe_state` (STOP, the dead-man; the power manager's response takes a function given to `sys_power_manager_init`) |

## 6. Descriptors

Every contract, device, block, error map and the board have a JSON descriptor next to their code, written against `templates/*.schema.json` (templates/AGENTS.md). `sh templates/sync.sh` regenerates `enums.json`, `errors.json` and the limits from the C sources, validates every descriptor, checks it against the code (IDs, sizes) and publishes to `runit-app/generated/`, which the app reads. Wire-visible changes in C always go with a sync.
