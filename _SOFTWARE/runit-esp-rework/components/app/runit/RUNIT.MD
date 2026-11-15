# runit — the composition

Component `runit-esp-rework/components/app/runit/`. The last layer: it knows every module and starts them in their order. No module depends on it; `main` calls `runit_start()` and nothing else. Rule of the layers: what two modules of a layer must do for each other, and neither may know the other, is done here.

## Files

| File | Role |
|---|---|
| `runit.h` / `runit.c` | `runit_start()`: the boot order (below) and the error sink |
| `runit_ids.h` | The ids of the board's own devices and providers (`RUNIT_DEV_*`, `RUNIT_PROVIDER_*`) |
| `connectivity/runit_connectivity.h` | The two steps below |
| `connectivity/runit_streams.c` | `runit_streams_init()`: the data matrix, the stream `EVENTS` and the events' forwarding |
| `connectivity/runit_ways.c` | `runit_ways_init()`: the console and the BLE pair, their roles and the routes of the streams |

## Boot order

`sys_nvs_init`, `sys_actions_init`, `sys_board_init`, `router_init`, `sys_event_init`, `sys_device_policy_init`, `sys_session_init`, `runit_streams_init` (the matrix), the error sink, `sys_system_init`, `sys_watch_init`, `sys_remote_init`, `runit_ways_init`, the ESP32-S3's GPIO (`0x00`, system), `sys_board_boot`. A step that fails is reported (`err_report`) and the next one runs.

- The console is a provider before the GPIO device, so ESP-IDF reserves its pins first (`PROVIDER_CONSOLE.MD`).
- The board's profile comes last, after every device of the board's own: its frames name the GPIO's pins.

## Connectivity

- **Streams** (`runit_streams_init`): starts the data matrix, gives it the flags of `RUNIT_STREAMS` (`EVENTS` `0x06`, urgent; the table is `runit_connectivity.h`'s), then `sys_log_init` (which declares `LOGS` and `ERRORS`). A listener subscribed with `SYS_EVENT_RUN_FIRST` and the filter `SYS_EVENT_ANY_NOTIFY` sends each event with `SYS_EVENT_NOTIFY` as it is to `EVENTS`: `sys_event` can't know the streams and the matrix can't know the events, so the listener is here. Where a stream goes is routes, made in `runit_ways_init`.
- **Ways** (`runit_ways_init`): the console provider and the runIT BLE pair, made by one CREATE of `provider_ble` (service `0xFFE0`, TX `0xFFE1`, RX `0xFFE2`, framed, locked, named `runit`: `runit_ids.h`; the cfg is a copy of the provider's), each given the role `SESSION` with the dead-man's default (`CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS`, with the safety flag when it is not 0), and the routes `S(ERRORS)` and `S(EVENTS)` to `T(provider, the stream's id)`: the app hears them as PUSH frames of its session. The logs have a BLE characteristic of their own: a second `provider_ble` (provider `0x04`, service `0xFFE8`, TX `0xFFE9`, raw, locked, named `logs`) with the role `RAW` and the route `S(LOGS)` to `P(0x04)`, so the lines go out as text to whoever is connected, with no session. The channels of the watches and the remote devices need no route here: their modules route them from any session. `sys_ble_start` comes last, once both are in the table.
- **Other ways** are not made here. A UART to a slave, an MQTT topic, several of either: each is the board's record's frames (the device `CREATE` if it has one, the provider `CREATE`, `ROLE`), by the board and how it is wired (`SYS_BOARD.MD`). The console and BLE are the only ways every board has, so a board with an empty record can be reached.

The console, BLE and GPIO are made as system devices and providers, by `sys_device_create_system` and `sys_data_provider_create_system`: they build the frame, the flag says they can't be removed.

## The error sink

Every reported chain goes to `error_sink`: `sys_device_policy_handle` answers it; `sys_log_report` keeps it and sends it on `ERRORS`; `sys_remote_on_error` queues it for the subscribers of the device it names. A policy response is logged, reported the same way and freed.

## Open

- The power manager's safe state (reason POWER) is wired here when the power manager is started.
