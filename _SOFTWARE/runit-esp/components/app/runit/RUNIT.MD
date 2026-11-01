# runit — the application

Component `components/app/runit/`. The application: the boot sequence, the safe state, the error policy wiring (action runner, error sink) and the board's stream routing. `main/main.c` only calls `runit_start()`.

| File | Contents |
|---|---|
| `runit.h` / `runit.c` | `runit_start()` (boot steps), `runit_run_boot_steps()`, `runit_enter_safe_state()`, router registration, the safe-state handler and leave of `sys_system` |
| `runit_error_policy.h/.c` | `runit_error_wiring_init()`: the action runner of the device error policy (the policy is `sys_device`'s, SYS_DEVICE.MD "Error policy"), the error sink, and the registry's state listener, which publishes the DEVICE events `SUSPENDED` / `RESUMED` (SYS_DEVICE.MD "Suspended and resumed") |
| `runit_board_cfg.h/.c` | `runit_board_connector_bindings_init()`: the logs connector on the BLE LOGS characteristic; `runit_board_power_init()`: the power layout |
| `runit_board_defs.h` | Onboard device IDs and power limits of board rev 1; the devkit's power limits |
| `board.json`, `board_devkit.json` | What the app knows of the board (templates/AGENTS.md): the PCB's devices, pins, power and terminals; the devkit's device 0, reserved pins and USB limits. Kept in step with `runit_board_cfg.c` by hand |
| `Kconfig` | `RUNIT_BLE_START`: off for QEMU. `RUNIT_BOARD`: the runIT PCB (default), an ESP32-S3 devkit (only the ESP GPIO, no power layout; `sdkconfig.devkit.defaults`) or no devices (QEMU and the test suites; `sdkconfig.qemu.defaults`) |

## Boot

In order; a failing step is logged and the boot goes on (one missing piece, such as no BLE controller under QEMU or no I2C pull-ups, doesn't take the clients' sessions down). `runit_start()` returns the first failure for `main` to report and reports the others. Ends with the log line `runIT up (N boot step(s) failed)`; a client sees the boot as a new session (a new `boot_id` in the board's HELLO, SYS_SESSION.MD).

| Step | Does |
|---|---|
| session | `sys_session_init` (SYS_SESSION.MD): the board id from NVS, the boot id, the timer. First: the connectors and the dead-man listen for its gone peers |
| streams | `sys_data_connector_init`, then `runit_error_wiring_init` (fault handlers, error sink) |
| log | `sys_log_init()`: the ESP_LOG hook on the logs connector, with the stored options |
| actions, events | `sys_actions_init`, `sys_event_init` |
| watches | `sys_watch_init` (SYS_WATCH.MD): its timer, the listener of gone peers that ends their watches, and the client of the watches added on other boards |
| remote devices | `sys_remote_init` (SYS_REMOTE.MD), only with `RUNIT_LINK_ROOT` or `RUNIT_LINK_CHILD`: sys_device's remote hook, so a device of another board can be made (CREATE_REMOTE) and used |
| onboard actions | `runit_onboard_actions_init` (below) |
| power | `runit_board_power_init`: `sys_power_manager_init` with the board's layout (`runit_board_cfg.c`, IDs and limits in `runit_board_defs.h`; a devkit: no rails, monitor or sink, only the USB limits) and `runit_enter_safe_state`. Before the router: it guards POWER_VREG |
| router | module routers (keys in `router_ids.h`, `sys_system_router` under `ROUTER_KEY_SYS`), the actions tap, the event action runner, the safe-state handler and leave, `sys_system_init` (the dead-man: a listener of the session's gone peers), `router_init`. The provider (`ROUTER_KEY_PROVIDER`), session (`ROUTER_KEY_SESSION`) and watch (`ROUTER_KEY_WATCH`) routers too |
| vm | `vm_init` (VM_CORE.MD): the event subscription, telemetry, retained values, the supervisor task. After the router (a program is loaded and run through `ROUTER_KEY_VM`) |
| uart, ble link | the providers: the UART console, the runIT BLE pair and LOGS (SYS_UART.MD, SYS_BLE.MD) |
| stream bindings | `runit_board_connector_bindings_init`: the logs connector bound to the BLE LOGS characteristic. Every other stream goes to the clients that subscribe to it |
| sessions | a client session (full access) on the UART console and on the runIT BLE pair, then `sys_data_provider_mark_system`: the providers so far are the board's own. With `RUNIT_LINK_ROOT` the sys_uart link is also a child session; with `RUNIT_LINK_CHILD` it is the parent session instead and no client is attached (a board with a parent takes none: the app reaches it through the root). The link overlays `sdkconfig.link_root.defaults` / `sdkconfig.link_child.defaults` (build.md "Two boards on a cable") |
| i2c | `sys_i2c_init` |
| devices | `runit_board_devices_init`, by `CONFIG_RUNIT_BOARD`: a devkit (`RUNIT_BOARD_DEVKIT`) gets only the ESP GPIO (ID 0); no devices (`RUNIT_BOARD_BARE`) skips the step. The PCB (`RUNIT_BOARD_PCB`): CREATE of the ESP GPIO (ID 0) and the TCA6424A (ID 1, 0x23, INT ESP 9, RST ESP 8), then TCA pins 22 and 23 as push-pull outputs and **pin 23 high: the status LED is lit once the boot got this far**. Pin 22 must be an output too (left low): as an input it floats and toggles at ~25 kHz, every toggle is an INT edge and the event queue overflows (measured on the PCB 2026-10-11). Then every other onboard chip in dependency order (`runit_board_defs.h` has the IDs and TCA pins): the two power muxes (7, 8; both switches off), ADS7128 (2, ALERT ESP 42, full scale 20000 mV), PCA9685 (3, OE TCA 0), TPS55289 rails A / B (10 / 11), INA3221 (12, all shunts reversed, 10 mOhm shunts), AP33772S (13), DAC53202 (4), DRV8962 #1 (5, ESP pins, 20 kHz) and #2 (6, PCA9685 CH 8..15, 50 Hz: the servos share the frequency); both as two full bridges, every channel braked, the chip's one current limit 2 A (`RUNIT_BOARD_DRV8962_LIMIT_MA`, an assumption: the legacy value is not recorded). A chip that fails is logged (`onboard device <name> failed`) and the rest are still created; only the GPIO or the expander failing stops the step. The power manager is initialised earlier and picks the rails and the INA3221 up when they appear |
| system devices | `sys_device_mark_system`: whatever the "devices" step created is the board's own; the REMOVE sweep (`sys_device_sweep`) leaves it |
| ble | `sys_ble_start` (if `RUNIT_BLE_START`) |
| project | `sys_project_init` (after `actions`: the options namespace) and, as the last step, `sys_project_boot`: the stored project is loaded, or loaded and started, on a router task as its boot option says (SYS_PROJECT.MD) |

Checked on a board: a stored project with a device section and boot LOAD is loaded after the boot (the section's commands are applied once the devices exist).

## Onboard actions

`runit_onboard_actions.c`: the firmware's own actions, stored at every boot with `sys_actions_set` (only written when they differ, so a client's edit of these IDs is replaced). The IDs are published in `runit-app/generated/common/limits.json` (`RUNIT_ACTION_ID_*`, `#@limit` in `Kconfig`), so the app keeps its own actions off them. Each is one write frame: a device-router sweep to device `0xFFFF` (SYS_DEVICE.MD "Sweeps over the router"), or a sys command:

| ID (Kconfig) | Does | Frame |
|---|---|---|
| 3 `RUNIT_ACTION_ID_RESUME` (user class, `0x00000003`) | resume all | `05 01 FF FF 03 00` (every device, oldest first) |
| 4 `RUNIT_ACTION_ID_SUSPEND` (`0x00000004`) | suspend all | `05 01 FF FF 02 00` (every device, newest first) |
| 5 `RUNIT_ACTION_ID_RESET` (`0x00000005`) | reset all devices | `05 01 FF FF 01 00` (every device, newest first; the drivers' RESET) |
| 6 `RUNIT_ACTION_ID_HARD_RESET` (`0x00000006`) | hard reset: restart the board | `01 01 03` (sys RESTART, `SYS_SYSTEM_OP_RESTART`, ~100 ms after the answer) |

Run with `06 01 01 <id u32>` (actions RUN, a write), or name the ID in a device's error policy. Hard reset is a board restart, so the VM, the devices and the BLE link are all rebuilt by the boot (and the project's boot option, if set). Reset all does not rewind the VM and does not re-apply a boot configuration. Boot is not a record: boot configuration is a device setting.

## Safe state

`runit_enter_safe_state()`: the VM stopped (`vm_exec_stop`, retained values saved), then every device suspended (`sys_device_sweep(SUSPEND)`). `sys_system` calls it on STOP and the dead-man (a client's session ended while armed, SYS_SYSTEM.MD); the power manager's response "safe state" goes through it too (`runit_power_safe_state` → `sys_system_enter_safe_state(SYS_SAFE_POWER)`), so every entry is noted with its reason for SYS `SAFE` (a read). The way out is SYS `SAFE_LEAVE`: `safe_leave` resumes every device (`sys_device_sweep(RESUME)`, oldest first); the client starts the program again (VM EXEC).

## Error policy wiring

The policy (walk, importance, CRITICAL → suspend all, the device's action) is `sys_device`'s (SYS_DEVICE.MD "Error policy"). runit gives it the **action runner**: `sys_actions_request(id, false)`, queued on the router, refused when the action's own failure (`ERR_ACTION_STEP{id}`) is in the chain.

A **policy per board** answers the errors about another board as a whole (`err_report_board`, ERRORS.MD): `runit_board_policy_set(board, policy)` (the board config's C code) chooses log only (the default: the record on the errors stream), stop the VM, or the safe state; the board sink writes the record (source `BOARD`) and runs it.

The error sink: every reported chain runs the policy first (whatever `send_errors` says), then becomes an error record on the errors connector (if `send_errors`). A failed response is logged and sent as a record, never reported (no policy on it).
