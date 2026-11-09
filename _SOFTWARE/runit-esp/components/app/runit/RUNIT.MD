# runit — the application

Component `components/app/runit/`. The application: the boot sequence, the safe state, the error policy wiring (action runner, error sink) and the board's stream routing. `main/main.c` only calls `runit_start()`.

| File | Contents |
|---|---|
| `runit.h` / `runit.c` | `runit_start()` (boot steps), `runit_run_boot_steps()`, `runit_enter_safe_state()`, router registration, the safe-state handler and leave of `sys_system` |
| `runit_error_policy.h/.c` | `runit_error_wiring_init()`: the action runner of the device error policy (the policy is `sys_device`'s, SYS_DEVICE.MD "Error policy"), the error sink, and the registry's state listener, which tells the screen adapter (`sys_lvgl_device_state`: it only sets a bit) and publishes the DEVICE events `SUSPENDED` / `RESUMED` / `RESET` (SYS_DEVICE.MD "Suspended, resumed, reset") |
| `runit_board_cfg.h/.c` | `runit_board_gpio_init()`: the ESP32-S3's GPIO device (ID `RUNIT_BOARD_GPIO_DEVICE_ID` 0, the board's own, no pins set up), the one device not in the profile; `runit_board_streams_init()`: the board's own streams, all OUT (logs, errors, telemetry, events); `runit_board_stream_bindings_init()`: the logs stream on the BLE LOGS characteristic; `runit_board_power_init()`: the power manager, with no rails until the profile gives them |
| `board.json`, `board_devkit.json` | What the app knows of the board and writes to it as its profile (SYS_BOARD.MD, templates/AGENTS.md): the devices, I2C buses, pins, power and terminals of the PCB; the devkit's device 0, buses, reserved pins and USB limits |
| `Kconfig` | `RUNIT_BLE_START`: off for QEMU. One build for every board: a board's role among boards is its sessions' (SYS_SESSION.MD "Boards, clients, the star") |

## Boot

In order; a failing step is logged and the boot goes on (one missing piece, such as no BLE controller under QEMU or no I2C pull-ups, doesn't take the clients' sessions down). `runit_start()` returns the first failure for `main` to report and reports the others. Ends with the log line `runIT up (N boot step(s) failed)`; a client sees the boot as a new session (a new `boot_id` in the board's HELLO, SYS_SESSION.MD).

| Step | Does |
|---|---|
| nvs | `nvs_init`: `sys_nvs_init` (SYS_NVS.MD) of the NVS partitions (`nvs`, `project` and `source`); the modules only open their namespaces |
| session | `sys_session_init` (SYS_SESSION.MD): the board id from NVS, the boot id, the timer. Before the streams and the dead-man: they listen for its gone peers |
| streams | `sys_stream_init`, `runit_board_streams_init` (the board's own streams, marked system), then `runit_error_wiring_init` (fault handlers, error sink) |
| log | `sys_log_init()`: the ESP_LOG hook on the logs stream, with the stored options |
| actions, events | `sys_actions_init`, `sys_event_init` |
| board | `sys_board_init` (SYS_BOARD.MD): the profile's JSON opened. Its router is `ROUTER_KEY_BOARD` |
| watches | `sys_watch_init` (SYS_WATCH.MD): its timer, the listener of gone peers that ends their watches, and the client of the watches added on other boards |
| remote devices | `sys_remote_init` (SYS_REMOTE.MD): sys_device's remote hook, so a device of another board can be made (CREATE_REMOTE) and used, and its task (the subscriptions to devices) |
| lvgl | `sys_lvgl_init` (SYS_LVGL.MD): LVGL, its task, the boot heap; empty until a project INSTALLs it on its display (`ROUTER_KEY_LVGL`) |
| power | `runit_board_power_init`: `sys_power_manager_init` (SYS_POWER_MANAGER.MD), with a USB port's limits and no rails until the profile gives them, and `runit_enter_safe_state`. Before the router: it guards POWER_VREG |
| router | module routers (keys in `router_ids.h`, `sys_system_router` under `ROUTER_KEY_SYS`), the event action runner, the safe-state handler and leave, `sys_system_init` (the dead-man: a listener of the session's gone peers), `router_init`. The provider (`ROUTER_KEY_PROVIDER`), UART (`ROUTER_KEY_UART`), session (`ROUTER_KEY_SESSION`) watch (`ROUTER_KEY_WATCH`), remote (`ROUTER_KEY_REMOTE`) and flasher (`ROUTER_KEY_FLASHER`) routers too |
| vm | `vm_init` (VM_CORE.MD): the event subscription, telemetry, retained values, the supervisor task. After the router (a program is loaded and run through `ROUTER_KEY_VM`) |
| uart, flasher, ble link | the providers: the UART console, the link, the runIT BLE pair and LOGS (SYS_UART.MD, SYS_BLE.MD); `sys_flasher_init` with the link's provider (SYS_FLASHER.MD: its BOOT and EN wires left floating). Further UART ports are made by the UART router (`ROUTER_KEY_UART`), by the board's profile or a client |
| stream bindings | `runit_board_stream_bindings_init`: the logs stream bound to the BLE LOGS characteristic. Every other stream goes to the clients that subscribe to it |
| sessions | the same on every board: a client session (full access) on the UART console and on the runIT BLE pair, a slave's on the link. A board's hello on the console makes that board this board's master: its other sessions end while it lasts (SYS_SESSION.MD "Boards, clients, the star"), so a board wired to another's link is its slave with no build of its own |
| esp32 gpio | `runit_board_gpio_init`: CREATE of the ESP32-S3's GPIO device (id 0, flag `SYS_DEVICE_CREATE_SYSTEM`, no configuration). Every build has it, so it is not in the profile; the profile's frames only name its pins. Before `board at start` |
| board at start | `sys_board_boot` (SYS_BOARD.MD): the RUN_ALL of the board's profile queued on the router, ahead of the project: the power layout, the devices, the I2C buses, their pins. With no profile the board has no devices and no buses, only the console and BLE |
| ble | `sys_ble_start` (if `RUNIT_BLE_START`) |
| project | `sys_project_init` (after `actions`: the options namespace) and, as the last step, `sys_project_boot`: the stored project is loaded, or loaded and started, on a router task as its boot option says (SYS_PROJECT.MD) |

Checked on a board: a stored project with a device section and boot LOAD is loaded after the boot (the section's commands are applied once the devices exist).

## Safe state

`runit_enter_safe_state()`: the VM stopped (`vm_exec_stop`, retained values saved), then every device suspended (`sys_device_sweep(SUSPEND)`). `sys_system` calls it on STOP and the dead-man (a client's session ended while armed, SYS_SYSTEM.MD); the power manager's response "safe state" goes through it too (`runit_power_safe_state` → `sys_system_enter_safe_state(SYS_SAFE_POWER)`), so every entry is noted with its reason for SYS `SAFE` (a read). The way out is SYS `SAFE_LEAVE`: `safe_leave` resumes every device (`sys_device_sweep(RESUME)`, oldest first); the client starts the program again (VM EXEC).

## Error policy wiring

The policy (walk, importance, CRITICAL → suspend all, the device's action) is `sys_device`'s (SYS_DEVICE.MD "Error policy"). runit gives it the **action runner**: `sys_actions_request(id, false)`, queued on the router, refused when the action's own failure (`ERR_ACTION_STEP{id}`) is in the chain.

A **policy per board** answers the errors about another board as a whole (`err_report_board`, ERRORS.MD): `runit_board_policy_set(board, policy)` (the board config's C code) chooses log only (the default: the record on the errors stream), stop the VM, or the safe state; the board sink writes the record (source `BOARD`) and runs it.

The error sink: every reported chain runs the policy first (whatever `send_errors` says), then becomes an error record on the errors stream (if `send_errors`), and with a link (`RUNIT_LINK_ROOT` / `_CHILD`) goes to `sys_remote_on_error`: a device's error outside a command reaches the boards subscribed to that device (SYS_REMOTE.MD "Subscriptions"). A failed response is logged and sent as a record, never reported (no policy on it).
