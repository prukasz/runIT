# Firmware architecture

Layers, data flows, boot sequence, agreed design and open findings. Module APIs are documented in each component's `*.MD`; this file covers how the modules fit together.

## 1. Layers

| Layer | Components | May depend on |
|---|---|---|
| 0 Base | `utils` (header-only), `sys_errors` | ESP-IDF only (plus each module's `errors/` map folder, §4.2) |
| 1 Services | `sys_i2c`, `sys_buffers`, `sys_event`, `ble`, `sys_data_connector`, `sys_settings` (persistent settings, NVS) | Layer 0 (`ble` → `sys_data_connector`: BLE implements the connector's provider interface; the connector knows no transport) |
| 2 Device core | `sys_device` (registry, lifecycle, dispatch helpers) | 0–1 |
| 3 Contracts | `sys_io`, `sys_power` (+ the power manager), `sys_hbridge` (contract types + domain dispatch) | 0–2 |
| 4 Devices | `devices/device_<chip>` (driver + adapter), `devices/devices` (aggregator, glob) | 0–3 |
| 5 Logic | `features`, `VM` | 0–3 (never a concrete device) |
| 6 Command | `codecs` (header-only decoders), `sys_interface` (class router), `sys_actions` (static/recorded actions) | 0–5; `sys_interface` and `sys_actions` don't depend on `codecs` |
| 7 App | `runit` (composition root: boot steps, board config, decoder registration, error sink, error policy), `main` | everything |

Rules:
- A lower layer never includes a higher one. Where it must call up, the higher layer **registers** a function at boot (§4.1).
- Declare only real dependencies in CMake. The build's only project warnings are the 16 intentional `sys_errors` error-map folders (§4.2).

## 2. Boot sequence (`runit_start()`)

`SE_init` → `runit_error_wiring_init` → `runit_board_i2c_init` → `sys_settings_init` → `runit_board_ble_init` → `sys_data_connector_init` → `runit_board_connector_bindings_init` → `runit_board_error_sink_init` → `SE_set_logging` → `sys_event_init` → `runit_board_power_init` (safe state, `sys_power_init` with the board power layout) → `runit_register_decoders` → `sys_interface_init` (starts the RX task) → `runit_board_bind_boot_action` → `sys_actions_init` → `runit_board_invoke_boot_action` (static action `CONFIG_SYS_ACTION_ID_BOOT` = `runit_board_devices_init`) → `vm_sub_init` → `vm_exec_start`.

- A failing step logs its name, sends the error, enters the safe state (`vm_exec_stop` + `sys_device_suspend_all`) and aborts boot. `main` then idles.
- Devices are installed by the boot **action**, not by a boot step, so the hard-reset static action re-runs the same install. Each onboard device is marked with `RUNIT_BOARD_DEVICE(id, d_<chip>_create(...))` (§4.5).
- Device install is skipped when `CONFIG_RUNIT_SKIP_DEVICE_INIT=y` (Kconfig, runIT Board menu). It's `y` in `sdkconfig` while testing on an empty devkit; set it to `n` on the board.

## 3. Data flows

- **Inbound command:** transport provider (queues one whole frame, de-framing a byte stream itself; BLE's wake callback calls `sys_data_connector_notify_rx()`) → `sys_data_connector` (`SYS_DATA_CONNECTOR_INTERFACE`, `receive()` also returns the frame's origin) → `sys_interface` RX task → class byte → codec decoder (`dec_*.h`, registered by runit) → `sys_*` / `sys_device_user_*` / `feature_*` / `vm_*` API → device adapter through `SYS_DEV_RESOLVE` / `SYS_DEV_DISPATCH` (§4.6).
- **Outbound:**
  - Error chains: `SE_send` → `enc_sys_errors` (private to `sys_errors`) → sink `send_packet` → errors connector.
  - Log lines (ESP_LOG and expanded chains): sink `send_log` → logs connector.
  - VM subscriptions: `vm_sub` → telemetry connector.
  - Command responses: `[0x05][seq][class][packet][status][data]` on the interface connector, sent only to the command's origin (`send_to`); `seq` is the byte the client put in front of its command (§4.8).
  - Every producer sizes frames to `sys_data_connector_max_payload()` (follows the BLE MTU); nothing on the path truncates, a too-long frame is `ERR_DATA_CONNECTOR_FRAME_TOO_LONG`. Design: SYS_DATA_CONNECTOR.MD.
- **Errors:** `SE_push_to_handler(chain)` → per node:
  - Device attribution (`device_id_of()` by tag) → per-device importance and level → registered device error policy (runit: critical → VM stop + `suspend_all`, then the configured action).
  - Non-device nodes go to the registered domain hook for `owner & 0xFF00` (ble, interface, actions, vm); others are skipped.
- **Events:** source (adapter, power manager, BLE, feature) → `sys_event_publish()` → inline listeners now (publisher's task; from an ISR, deferred to the event task) → one queued copy → event task → each queued listener: C handler, routes (VM = slot 0), user action, system action (§4.11).

## 4. Agreed design

### 4.1 Wiring: explicit registration from `runit`
Every upward call goes through a `*_register_*()` function that `runit` (the composition root) calls in its boot steps, so the wiring and its order are visible in `runit.c` / `runit_board_cfg.c` / `runit_decoders.c` / `runit_error_policy.c`. No weak symbols and no registering constructors (only `utils.h` static RTOS objects use constructors).

| Registration | Where | Target |
|---|---|---|
| Decoder classes (9) | `runit_register_decoders()`, before `sys_interface_init()` | `dec_*_decode` (separate file because decoder headers redefine `OWNER`) |
| Error sink | `runit_board_error_sink_init()` | logs + errors connectors (§4.2) |
| Transport providers | `runit_board_connector_bindings_init()` | `sys_ble_provider_register(RUNIT_DATA_PROVIDER_BLE)` (in `ble`), then the bindings of the 4 system connectors; the BLE core stays consumer-agnostic (`sys_ble_char_link_rx_wake`) |
| `SE_register_device_router` | `runit_error_wiring_init()` (first step after `SE_init`) | `sys_device_report_error_with_level`, `sys_device_is_ignored` |
| `sys_device_register_error_policy` | 〃 | `runit_device_policy`: CRITICAL → VM stop + suspend all, then the device's action. Default without one: CRITICAL → suspend all |
| `SE_register_system_hook` | 〃 | `runit_system_hook`: unattributed CRITICAL → VM stop + suspend all |
| `SE_register_domain_hook` (table, `CONFIG_SYS_ERRORS_MAX_DOMAIN_HOOKS`) | 〃 | ble (failure event), interface (suspend RX), actions (abort recording), vm (latch + stop). A domain without a hook is skipped; add one only for real containment |
| `vm_exec_register_action_request` | 〃 | `sys_actions_request` (queued; the VM task never runs an action) |
| `sys_event_register_action_executor` / `sys_event_register_route` | 〃 | `sys_actions_invoke`, `vm_event_route` (`CONFIG_SYS_EVENT_ROUTE_VM`) |
| `sys_power_register_safe_state` | `runit_board_power_init()` | `runit_enter_safe_state` |

### 4.2 `sys_errors` as a base layer: keep maps, inject the sink
- **Sink:** `sys_errors` knows no transport. `SE_register_sink(&(sys_error_sink_t){send_log, send_packet, packet_max_len})`, bound by runit, looked up per send. Unbound output is dropped (serial mirroring still works).
- **Encoder:** `enc_sys_errors.h` is private to `sys_errors`; `codecs` holds decoders only.
- **Error maps:** each module's `sys_error_<module>.h` (tags + owners) sits in the module's `errors/` folder and `sys_errors` aggregates them through include dirs (pure data, not code dependencies). Only map folders are on the path, so no module API leaks. 16 expected CMake warnings, one per folder. Details: SYS_ERRORS.MD "Error maps and aggregation".

### 4.3 Discarded `err_h` is a compile error
- Every function returning `err_h` is `SE_MUST_USE`; an intentional drop is `SE_release(call)`. Don't use an `err_h` call as a condition (the chain leaks).
- Steps that must all run (teardown, suspend, per-channel sweeps, fault notification) accumulate with `SYS_DEV_TEARDOWN_STEP` / `SYS_DEV_TEARDOWN_DRIVER_STEP` and return the first error.
- A locked dependency pin is driven only through `sys_io_set_locked_level()`, which re-locks on every path.
- The compiler doesn't check `esp_err_t` results or `err_h` returned through function pointers (vtables, event handlers; the event dispatcher reports handler errors).

### 4.4 Features: on hold
Features need their own analysis before any redesign (type safety, lifecycle, fault handling, relation to devices). Change `components/features/` only as far as other work requires. Findings: F-2, F-17.

### 4.5 Onboard vs runtime devices
- Users plug devices in and remove them at runtime (install/uninstall packets). Onboard ones are installed by the board config and flagged with `sys_device_set_onboard()`.
- **Firmware rule:** a user can't uninstall (and so can't replace) an onboard device. User paths (packets, replayed recorded actions) call `sys_device_user_uninstall[_all]`: the single call returns `ERR_DEV_ONBOARD {dev_id}`, the `_all` sweep skips them. Everything else is app policy.
- System paths (boot action, hard reset, fault handling) use the plain `sys_device_*` functions.
- `ERR_DEV_ONBOARD` is a rejected command, not a device fault, so it's not in `device_id_of()`.

### 4.6 Contracts, dispatch and units
- **Naming:** `sys_<domain>[_<kind>]_contract_t`, members without a domain prefix, adapter instances `static const … s_<chip>_<kind>_contract`. Dispatch APIs follow the component (`sys_power_vreg_*`, `sys_power_monitor_*`, `sys_power_usb_pd_*`).
- **One dispatch path:** `SYS_DEV_RESOLVE` (device state + contract + function) → `SYS_DEV_DISPATCH`. `sys_io` adds pin range/lock checks, `sys_power` wraps its budget around resolve.
- **Feature IDs** = member index (`SYS_DEV_FEATURE_ID`), named by each contract's NULL-terminated `<contract>_feature_names[]` (`_Static_assert`-checked). No feature enums.
- **Units** in real notation (`_mV`, `_mA`, `_mW`, `_Hz`, `_us`, `_ms`); macros / Kconfig stay caps; ESP-IDF fields keep their names. Measured outputs are `int32_t`; where a value must be non-negative it's checked (`SE_CHECK_IN_RANGE_I32`).

### 4.7 Owners
Every module raises errors under its own owner domain (`owner & 0xFF00` selects the domain fault hook):

| Domain | Module | Domain | Module |
|---|---|---|---|
| `0xA1` | sys_device | `0xA9` | VM |
| `0xA2` | sys_i2c | `0xAA` | sys_actions |
| `0xA3` | sys_io | `0xAB` | sys_event |
| `0xA4` | sys_power | `0xAC` | sys_hbridge |
| `0xA5` | ble | `0xAD` | sys_data_connector |
| `0xA6` | sys_interface + `OWNER_DEC_*` decoders + error encoder | `0xAE` | features |
| `0xA7` | sys_buffers | `0xAF` | runit |
| `0xA8` | sys_errors | `0xD0` | devices |

- System modules use per-function owners (`OWNER_<MODULE>_<FUNCTION>`); features per-part, runit board / error-policy owners.
- Drivers that return `esp_err_t` define no `OWNER` (the adapter owns the error); the DRV8962 driver (`err_h`) uses `OWNER_DEVICE_DRV8962`.

### 4.8 Command responses
- A live command is `[seq][class][packet][payload]`; the RX task strips `seq` (decoders, recorded actions and replays never see it). Every live command is answered on the interface connector, only to its origin: `[0x05][seq][class][packet][status][data]`. `seq`/`class`/`packet` echo the request; the app matches by `seq` with a timeout (a lost response fails one command, not every later one). Only stream `0x05` carries `seq`; the other streams are unsolicited pushes. `status` (`sys_interface_status_e`): `0` OK + the getter's data, `1` error + `u16 tag, u16 owner` of the root cause (the full chain still goes to the errors stream).
- Getters declare `packet_<name>_response_t` next to the request and call `sys_interface_respond()`. The contracts generator publishes `response` layouts and the `response_stream`.
- Frames replayed by `sys_actions` aren't answered (response capture is task-local, on only while the RX task decodes a live frame).

### 4.9 Power manager
Design and API: SYS_POWER.MD.
- One board input feeds a fixed reserve (the 3.3 V buck) and the budgeted vreg rails; the layout is the board config (`sys_power_board_t`), so a board revision changes only the description.
- Budget = measured input voltage × source current (USB-C offer / entered PSU / battery = board cap / unknown = 1 A), capped by the hardware trip (20 V / 5.5 A).
- A user current limit makes a rail fixed (must fit); the other rails split what's left equally.
- Response matrix per event (`NOTIFY / DISABLE / RESET / SAFE_STATE`): board defaults + runtime packet, not persisted. The manager is a queued system listener of every power event (§4.11).
- PSU / battery setup is persisted through `sys_settings` (`pwr_psu`, `pwr_battery`).

### 4.10 Error handling
Rules: [errors.md](errors.md).
- Severity by consequence: only real system faults are CRITICAL; pool exhaustion is a LOW fallback node.
- Responses run synchronously; logging and telemetry go through a low-priority log task (`SE_log`); repeated chains are suppressed for 2 s.
- Stable tag IDs `X(tag, id, level, struct)` (high byte = owner domain); no packet versioning.
- Full trace kept (`SE_TRY` adds a hop); no line numbers (per-function owners locate the error).

### 4.11 Events: one publisher, many listeners
Full design: SYS_EVENT.MD.
- `sys_event_t {domain, device_id, channel, event, value, hops}`; domains IO / POWER / HBRIDGE / BLE / FEATURE. Sources publish (`sys_io_publish` / `sys_power_publish` / `sys_hbridge_publish` / `sys_ble_publish`) without knowing the listeners.
- One subscription table (`CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS` 32), match per field exact or `SYS_EVENT_ANY`.
- **Inline** = C only, in the publisher's task; may block on I/O, never on a delay or timeout. **Queued** = C handler and/or routes / actions, in the event task.
- Loops: republish with `SYS_EVENT_CAUSED_BY(cause)`; dropped at `CONFIG_SYS_EVENT_MAX_HOPS` (4) with `ERR_EVENT_LOOP`.
- Packets (class `0x09`) make only queued route / action subscriptions (`user`) and can't remove system ones.
- Arming is device config, not subscription: `sys_io_configure_intr`, `sys_power_monitor_set_alert`. Regulator and bridge faults are always armed.
- Chip alert pins chain with `sys_io_subscribe_pin` (inline, system); features republish under FEATURE.

## 5. Open findings

IDs are stable, so gaps are expected. Fixed findings are removed.

| ID | Finding | Where |
|---|---|---|
| F-2 | Feature type confusion: one ID space for all feature types, `feature_get_by_id` returns untyped `void*`, and each feature casts it to its own struct. A servo call on an H-bridge ID writes into the wrong struct (on hold, §4.4) | `features/registry`, `feature_servo.c`, `feature_hbridge.c` |
| F-17 | Features: heap linked list vs device fixed table; no lifecycle (not suspended on fault); `teardown` returns `void` (teardown errors are released with a TODO); errors are `ERR_BASE_NOT_FOUND(0)` without a feature ID (on hold, §4.4) | `features/` |

## 6. Suggested order

1. Power: confirm the rev 1 facts (INA3221 channel order, source indicator pins) in `runit_board_defs.h` / `runit_board_cfg.c`; test power and events on the board.
2. Naming cleanup (conventions.md §5.2).
3. App: response stream decoder, power page, event subscriptions (runit-app).
4. Features analysis (§4.4) → F-2, F-17.
