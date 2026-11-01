# Firmware architecture

How the components fit together. Each component's own `*.MD` holds its API and wire format; this file only links them. Paths are under `runit-esp/components/`.

## 1. The idea

A plug-and-play board for hobbyists. The board exposes what it has (devices, contracts, the VM, records) as **commands** on one router; a client (the app, a test host, the board itself) builds and drives everything through them. Everything the board says comes back as **streams** in the same command shape. A user program is compiled off-device, uploaded as VM records, stored as a project and run by the VM. Nothing on the board decides on its own beyond safety: the error policy and the power manager.

## 2. Layers

| Layer | Components | May depend on |
|---|---|---|
| core | `utils`, `time`, `errors`, `cmd`, `router` | ESP-IDF only (and earlier core) |
| sys | `sys_data_connector`, `sys_watch`, `sys_uart`, `sys_ble`, `sys_log`, `sys_i2c`, `sys_event`, `sys_device`, `sys_io`, `sys_power`, `sys_hbridge`, `sys_power_manager`, `sys_actions`, `sys_project` | core, earlier sys |
| devices | `devices/device_<chip>` (one driver each), `devices/devices` | core, sys (the contracts they implement, `sys_i2c`, `sys_io` for pins) |
| vm | `vm_core`, then `vm_blocks` | core, sys; `vm_blocks` also the devices whose own ops it has a block for |
| app | `app/runit` (boot, wiring, board config), `main` (only `runit_start()`) | everything |

Rules:
- A lower layer never includes a higher one. Where it must call up, the higher layer **registers** a function at boot (§5), always from `runit`.
- Self-registration by constructor exists only for tables of types: device types (`SYS_DEVICE_TYPE_REGISTER`), block types (`VM_BLOCK_TYPE_DEFINE`), error tag maps (`ERR_MODULE_DEFINE`). Those components are linked `WHOLE_ARCHIVE`.
- Declare only real dependencies in CMake (`REQUIRES` / `PRIV_REQUIRES`).

## 3. Boot

`runit_start()` runs the boot steps in order: RUNIT.MD "Boot" (session, streams, log, actions and events, watches, onboard actions, power, router, vm, the providers (uart, ble link), stream bindings, sessions, i2c, onboard devices, system devices, ble, project). A failing step is reported and the boot goes on; the last line is `runIT up (N boot step(s) failed)`.

## 4. Flows

- **Inbound command:** a provider's driver (`sys_uart`: COBS + CRC16 frames on the console; `sys_ble`: the runIT RX characteristic) delivers a whole frame to its one user, the session (SYS_SESSION.MD) → a client's REQUEST `[frame byte][dst][src][seq u16][key][dir][code]...[payload]` becomes an `err_cmd_t` → `router_submit(cmd, session_answer, NULL)` → urgent or work task → root key (`router_ids.h`) → the module's router → its API → the answer to the command's `done`: the session frames it (ANSWER, status in the frame byte, the same `seq`) back to `src`. The board's own commands: `router_inject` (src 0xFE). `dir` picks the command's read or write side; a command with both is a mirror (the read answers what the write takes, CMD.MD "Read and write").
- **Outbound streams:** `sys_data_connector` (SYS_DATA_CONNECTOR.MD): a connector is one byte (logs 0x03, errors 0x04, telemetry 0x02, events 0x06, user ones any other byte but 0x00 and 0x05). Its targets: bindings to providers (the payload as is; the board config binds logs to the BLE LOGS characteristic) and peers subscribed over their session (SUBSCRIBE: a STREAM frame `[connector][payload]` to that client). Answers never go through a connector. Payloads are sized by `sys_data_connector_max_payload`.
- **Sessions and the dead-man:** every client opens a session (HELLO; the board gives it its id) and keeps it with PINGs; a session that ends (silence, its provider down) makes its peer gone: its subscriptions end, and an armed dead-man enters the safe state (SYS_SYSTEM.MD).
- **One shape both ways:** stream payloads are commands without `dir`: an event is `[0x07][NOTIFY]...`, VM telemetry is `[0x0B][DATA | ADD_OBJECTS]...`, an error record carries the request that failed (with its `dir`). A client parses everything with one decoder, and a frame can be sent back as a write (`dir` 1 after the key) or stored in a record.
- **Errors:** a chain (ERRORS.MD) is returned up to whoever ends it; `err_report` → the sink in `runit_error_policy.c` → the device error policy (`sys_device_handle_error`, SYS_DEVICE.MD "Error policy") → an error record on the errors stream (ERRORS.MD "Error record"). Errors of a routed command are answered and reported with that command.
- **Events:** a source publishes (`sys_event_publish`, SYS_EVENT.MD): NOTIFY events go to the events stream first, then the device listeners (inline, their ON_EVENT), then the function listeners in the event task (the VM's wildcard subscription, the power manager). A device's `suspended` flag changing (SUSPEND, RESUME, a sweep: one event for the sweep) is the DEVICE contract's `SUSPENDED` / `RESUMED`: `sys_device` tells the state listener runit sets, which publishes it (SYS_DEVICE.MD "Suspended and resumed").
- **Watches:** a peer's ADD (`ROUTER_KEY_WATCH`) names a read, a trigger and a period; `sys_watch` injects that read as a command of the board on its tick (or on a matching event) and sends each answer as an entry in a STREAM frame to the peer, from the router's work task, so entries and answers to that peer keep their order (SYS_WATCH.MD). The peer's session ending removes its watches.
- **Devices:** `[0x05][dir][id u16][op u16][args]` → `sys_device_call` → the driver's op table; contract ops are `0xCCxx` (contract ID, op), a driver's own ops `0xFFxx` (SYS_DEVICE.MD, SYS_IO.MD, SYS_POWER.MD, SYS_HBRIDGE.MD).
- **Records and projects:** any command frame can be stored in a record (SYS_ACTIONS.MD) and run later; a recording keeps the write frames only. A project is a stream of such frames saved as records, loaded or run at a boot as its boot option says (`sys_project_boot_e`: none / load / run, SYS_PROJECT.MD). Settings are records of a module's write frame; nothing else replays by itself.

## 5. Wiring (all in `app/runit`)

| What | Where |
|---|---|
| Module routers under their root keys, the actions tap, the safe-state handler | `runit.c` (boot step `router`) |
| Error sink, device policy action runner, VM stop on a critical device error (`vm_stop_on_fault`) | `runit_error_policy.c` (`runit_error_wiring_init`) |
| Which stream goes to which provider (logs → BLE LOGS); the clients' sessions (UART, BLE pair) | `runit_board_cfg.c` (`runit_board_connector_bindings_init`), `runit.c` (`sessions_init`) |
| Power layout, limits and the safe state for the power manager | `runit_board_cfg.c` (`runit_board_power_init`), `runit_board_defs.h` |
| Onboard devices and their IDs | `runit_board_devices_init`, `runit_board_defs.h`, `board.json` |
| The firmware's own actions (resume / suspend / reset all, hard reset) | `runit_onboard_actions.c` |
| Safe state: VM stopped (retained values saved), then every device suspended | `runit_enter_safe_state` (`sys_system`: STOP, the dead-man; critical errors) |

## 6. Descriptors

Every contract, device, block, error map and the board have a JSON descriptor next to their code, written against `templates/*.schema.json` (templates/AGENTS.md). `sh templates/sync.sh` regenerates `enums.json`, `errors.json` and the limits from the C sources, validates every descriptor, checks it against the code (IDs, sizes) and publishes to `runit-app/generated/`, which the app reads. Wire-visible changes in C always go with a sync.
