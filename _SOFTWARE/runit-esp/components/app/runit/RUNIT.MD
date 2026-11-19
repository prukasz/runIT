# runit — the composition

Component `runit-esp/components/app/runit/`. The last layer: it knows every module and starts them in their order. No module depends on it; `main` calls `runit_start()` and nothing else. Rule of the layers: what two modules of a layer must do for each other, and neither may know the other, is done here.

## Files

| File | Role |
|---|---|
| `runit.h` / `runit.c` | `runit_start()`: the boot order (below) and the error sink |
| `connectivity/runit_connectivity.h` | `runit_data_matrix_setup()` |
| `connectivity/data_matrix_setup.c` | The board's data: its stream, providers, roles and routes as tables and the one pass over them |
| `runit_ids.h` | The ids of the board's own devices and providers (`RUNIT_DEV_*`, `RUNIT_PROVIDER_*`) |

## Boot order

`sys_nvs_init`, `sys_actions_init`, `sys_board_init`, `router_init`, `sys_event_init`, `sys_device_policy_init`, `sys_session_init`, `runit_data_matrix_setup` (the matrix, the providers, their roles and routes, BLE), the error sink, `sys_system_init`, `sys_watch_init`, `sys_remote_init`, the ESP32-S3's GPIO (`0x00`, system), `sys_board_boot`. A step that fails is reported (`err_report`) and the next one runs.

- The console is a provider before the GPIO device, so ESP-IDF reserves its pins first (`PROVIDER_CONSOLE.MD`).
- The board's profile comes last, after every device of the board's own: its frames name the GPIO's pins.

## The board's data (`connectivity/data_matrix_setup.c`)

- **Streams**: starts the data matrix, then the modules that have streams: `sys_log_init` (`LOGS`, `ERRORS`) and `sys_event_stream_init` (`EVENT_STREAM_NOTIFY` out, `EVENT_STREAM_REMOTE` in: `SYS_EVENT_STREAM.MD`). Each module declares and sends its own. Where a stream goes is routes, in `k_routes`.
- **Ways**: the board's whole matrix is three tables (`k_providers`, `k_roles`, `k_routes`) applied in one pass, then `sys_ble_start`. The setup makes the runIT BLE service (`0xFFE0`, `sys_ble_service_create`) and, after the providers, locks it; the characteristics join it. The console provider and the runIT BLE pair, made by one CREATE of `provider_ble` (TX `0xFFE1`, RX `0xFFE2`, framed, named `runit`: `runit_ids.h`; the cfg is `provider_ble.h`), each given the role `SESSION` with the dead-man's default (`CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS`, with the safety flag when it is not 0), and the routes `S(ERRORS)` and `S(EVENT_STREAM_NOTIFY)` to `T(provider, the stream's id)`: the app hears them as PUSH frames of its session. The logs have a BLE characteristic of their own: a second `provider_ble` (provider `0x04`, TX `0xFFE9` in the same service, raw, named `logs`) with the role `RAW` and the route `S(LOGS)` to `P(0x04)`, so the lines go out as text to whoever is connected, with no session. The channels of the watches and the remote devices need no route here: their modules route them from any session. `sys_ble_start` comes last, once both are in the service.
- **Other ways** are not made here. A UART to a slave, an MQTT topic, several of either: each is the board's record's frames (the device `CREATE` if it has one, the provider `CREATE`, `ROLE`), by the board and how it is wired (`SYS_BOARD.MD`). The console and BLE are the only ways every board has, so a board with an empty record can be reached.

The console, BLE and GPIO are made as system devices and providers, by `sys_device_create_system` and `sys_data_provider_create_system`: they build the frame, the flag says they can't be removed.

## The error sink

Every reported chain goes to `error_sink`: `sys_device_policy_handle` answers it; `sys_log_report` keeps it and sends it on `ERRORS`; `sys_remote_on_error` queues it for the subscribers of the device it names. A policy response is logged, reported the same way and freed.

## Open

- The power manager's safe state (reason POWER) is wired here when the power manager is started.
