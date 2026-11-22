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

`nvs_store_init`, `board_id_init`, `sys_actions_init`, `sys_board_init`, `router_init`, `sys_log_init` (the modules' ports before the links to them), `runit_data_matrix_setup` (the providers, the sessions and their links, BLE), the error sink, the ESP32-S3's GPIO (`0x00`, system), `sys_board_boot`. A step that fails is reported (`err_report`) and the next one runs.

- The console is a provider before the GPIO device, so ESP-IDF reserves its pins first (`PROVIDER_CONSOLE.MD`).
- The board's profile comes last, after every device of the board's own: its frames name the GPIO's pins.

## The board's data (`connectivity/data_matrix_setup.c`)

- **Ports**: the modules register their own ports of the data matrix before the setup links them: `router_init` (`ROUTER`) and `sys_log_init` (`LOGS`, `ERRORS`) in the boot order, `sys_watch` (`WATCH`), `sys_device_remote` (`REMOTE`) and `sys_event_stream` (`EVENTS`) in a constructor before `app_main`; the setup only links them.
- **Ways**: the setup makes the runIT BLE service (`0xFFE0`, `sys_ble_service_create`) and, after the providers, locks it; the characteristics join it. The providers (`k_providers`, ids and UUIDs in `runit_ids.h`): the console; `runit-session`, the runIT BLE session (one CREATE of `provider_ble`: the one characteristic `0xFFE1`, written and notified, framed, named `runit`; the cfg is `provider_ble.h`); `runit-logs` (`0xFFE3`, raw, named `logs`) and `runit-telemetry` (`0xFFE4`, raw, named `telemetry`), both in the same service. Two sessions (`k_sessions`, each with the dead-man's default `CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS`, with the safety flag when it is not 0), each sub-port linked its ways (IN: the sub-port to its module's port, what the peer sends; OUT: the module's port to the sub-port, what the peer hears): the BLE pair (`k_ble_subs`) shows the commands (`ROUTER`, IN), the errors and the events (IN and OUT) and the remote devices' frames (`REMOTE`, IN: what goes out goes to the sub-port by name); the console (`k_console_subs`) shows everything: the commands, the errors, the events, the watches' entries (`WATCH`, IN and OUT), the log lines (`LOGS`, OUT) and the remote devices' frames (`REMOTE`, IN). Without a session, `LOGS` is linked to `runit-logs` (the lines as text) and `WATCH` to `runit-telemetry` (the watches' entries as they are), so whoever is connected gets them. The router's answers go back to the port a command came from by name. `sys_ble_start` comes last, once the characteristics are in the service.
- **Other ways** are made at run time, not here. A UART to a slave, an MQTT topic, several of any: each is the board's record's frames (the device `CREATE` if it has one, the provider `CREATE`, the session's `ATTACH`, then the data matrix's `CONNECT`s of its sub-ports), by the board and how it is wired (`SYS_BOARD.MD`). The console and BLE are the only ways every board has, so a board with an empty record can be reached.

The console, BLE and GPIO are made as system devices and providers, by `sys_device_create_system` and `sys_data_provider_create_system`: they build the frame, the flag says they can't be removed.

## The error sink

Every reported chain goes to `error_sink`: `sys_device_policy_handle` answers it; `sys_log_report` keeps it and sends it on `ERRORS`; `sys_device_remote_on_error` queues it for the subscribers of the device it names. A policy response is logged, reported the same way and freed.

## Open

- The power manager's safe state (reason POWER) is wired here when the power manager is started.
