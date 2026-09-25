# Devices — drivers, adapters, install packets

API reference: [components/system/sys_device/SYS_DEVICE.MD](../../../components/system/sys_device/SYS_DEVICE.MD) (registry, lifecycle, macros). Board wiring and device IDs: [HARDWARE.md](../runit-root/HARDWARE.md). Annotation grammar: `data-structures/auto-annotations/device/device-annotations.md`.

## 1. Anatomy of a device

One device = one ESP-IDF component `components/devices/device_<chip>/`. It is picked up automatically through `EXTRA_COMPONENT_DIRS "components/devices"`.

```
components/devices/device_<chip>/
├── CMakeLists.txt          SRCS driver + adapter; REQUIRES sys_errors sys_io sys_device (+ sys_i2c / sys_power / sys_hbridge as used)
├── include/device_<chip>.h PUBLIC: d_<chip>_cfg_t + d_<chip>_create() — nothing else
├── driver_<chip>.h/.c      PRIVATE: registers, bus transfers, chip logic. Knows nothing about sys_device
└── adapter_<chip>.c        PRIVATE: binds the driver to system contracts + lifecycle ops
```

Plus, outside the component:

| File | Purpose |
|---|---|
| `components/devices/devices/include/devices.h` | Includes every `device_<chip>.h` |
| `components/devices/devices/errors/devices_owners.h` | `OWNER_DEVICE_<CHIP>` (0xD0xx) in `PROVIDER_OWNER_MAP` |
| `components/codecs/decoders/device/dec_device_<chip>.h` | Install packet (`HEADER_packet_sys_device_install_<chip>_t`, 0x4x), decoder → `d_<chip>_create`, and **`//@` annotations** describing the device and its contract ops for the app |
| `components/codecs/decoders/dec_sys_device_install.h` | Includes the decoder header and adds `SYS_CONTRACTS_DEVICE_<CHIP>_PACKET_LIST(X)` to `SYS_CONTRACTS_INSTALL_PACKET_LIST` |
| `components/codecs/CMakeLists.txt` | `device_<chip>` in `REQUIRES` |
| `data-structures/devices/device_<chip>.generated.json` | Generated from the decoder annotations. Never edit by hand |
| `components/runit/runit_board_defs.h` / `runit_board_cfg.c` | Only for **onboard** chips: `DEVICE_ID_<CHIP>` (`//@STATIC_DEVICE`) and a `d_<chip>_create(...)` call in `runit_board_devices_init()` |

### Layers and flow

```
app / VM ──packet 0x01/0x4x──► decoder (dec_device_<chip>.h) ──► d_<chip>_create(cfg)
                                                                    │ SYS_DEVICE_CREATE
                                                                    ▼
sys_device registry [device_id] ── cls->ops.install ──► adapter ──► driver ──► sys_i2c / sys_io
                                                            ▲
sys_io_* / sys_power_* / sys_hbridge_* (by device_id) ──────┘ contract vtable call
```

- **Contracts** (`sys_device_contract_type_e`): `IO`, `POWER_VREG`, `POWER_MONITOR`, `POWER_USB_PD`, `HBRIDGE`. The vtable types are defined by `sys_io`, `sys_power` and `sys_hbridge`. A device may provide several contracts (AP33772S: VREG + USB_PD + MONITOR).
- **Class vs instance:** each adapter declares one `static const sys_device_class_t` (name, `contracts[]`, `ops`). `sys_device` owns the instance: the registry slot, a heap copy of the cfg, state and error-handling settings.
- **Lifecycle ops:** `install`, `uninstall`, `reset`, `suspend`, `resume`, `freeze`, `sync`. A missing op makes the manager return `ERR_BASE_NOT_SUPPORTED`. **`suspend` is the fault safe state** (critical faults call `sys_device_suspend_all()`), and `resume` brings the device back. `freeze` latches an input snapshot and defers output writes (a process image); `sync` flushes and releases. Freeze is under review (§5.2).

## 2. Adding a new device — checklist

1. **Driver** `driver_<chip>.c/.h`:
   - Chip logic only. Use `sys_i2c_*` for bus transfers, never raw IDF I2C. **Don't register with `sys_i2c` in the driver**: the adapter's install does `sys_i2c_add_driver` (which also probes the chip).
   - No logging. A failure inside a driver-owned task is reported through a callback set by the adapter (see AP33772S `error_callback`).
   - `<chip>_new(...)` allocates the handle, `<chip>_start` / ops act on it.
   - Returns `esp_err_t`. Exception: a chip controlled only through other devices' pins (DRV8962) has `sys_io` as its bus, so its driver returns `err_h`.
   - Handle type `<chip>_h`, prefix `<chip>_` (naming: [conventions.md](conventions.md) §5).
   - Implement the full chip feature set unless told otherwise.
2. **Public header** `include/device_<chip>.h`:
   - `d_<chip>_cfg_t` with **`uint8_t device_id` as the first member** (`SYS_DEVICE_CREATE` reads it).
   - Dependencies on other devices' pins are `sys_io_pin_ref_t` fields. Document that "unused" must be written `SYS_IO_PIN_NONE`, because a zero-filled field means device 0 / pin 0.
   - Declare only `err_h d_<chip>_create(const d_<chip>_cfg_t* cfg);`.
3. **Adapter** `adapter_<chip>.c`, following the reference pattern `device_pca9685/adapter_pca9685.c`:
   - `#define OWNER OWNER_DEVICE_<CHIP>`, `TAG`.
   - A context struct whose first member is `sys_device_adapter_base_t base`, followed by `d_<chip>_cfg_t cfg`, then any frozen/deferred state.
   - Contract functions start with `SYS_DEV_GET_ADAPTER_CONTEXT(ctx_t, <chip>_h, ctx, hw, handle)`. Validate pins with `VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, MASK)`.
   - Validate every value the driver would reject **before** calling it, with `SE_CHECK_IN_RANGE` (the value, min and max go into the error). Examples: I2C address in install (INA3221, TPS55289), voltage limit (AP33772S).
   - Call the driver through `SYS_DEV_CHECK_DRIVER_CALL(call, ctx)`, which gives `ERR_DEV_DRIVER_FAILED {dev_id, line}` over the ESP code. For an `esp_err_t` obtained elsewhere (a direct ESP-IDF call, a driver-task callback), use `SYS_DEV_DRIVER_ERR(code, ctx)`.
   - IO errors take `SYS_DEV_GET_ID(ctx)` as their first field (`ERR_IO_PIN_*` payloads are `{dev_id, pin, …}`). Use designated initializers when the order isn't obvious.
   - **No `ESP_LOG*`** in drivers or adapters, and no `TAG`. Context goes into errors. Success messages aren't needed: `sys_device` logs installs.
   - Freeze-aware writes: `IF_SYS_DEV_FROZEN(ctx) { store deferred value; return NULL; }`. `device_sync` flushes the deferred values and clears the freeze.
   - `static const` contract vtables (unsupported ops = `NULL`), and one `static const sys_device_class_t` with `.contracts = {[SYS_DEVICE_CONTRACT_X] = &s_vtable}` (no casts). Never store runtime state in a vtable.
   - **install:**
     - `SYS_DEV_CTX_NEW`, then `<chip>_new`.
     - Each step through `SYS_DEV_INSTALL_STEP(expr, "what")`, followed by `SYS_DEV_STEP_DONE(ctx, STEP_BIT)`.
     - Probe the chip (`sys_i2c_device_present`).
     - Set up dependency pins with `sys_io_set_mode`, then `sys_io_lock_pin`.
     - An alert / IRQ pin: `sys_io_subscribe_pin(pin, device_event_handler, ctx, &ctx->intr_sub)` (step bit), `sys_io_configure_intr(pin, {.mode = FALLING_EDGE})` (no debounce), lock. The handler (`err_h (const sys_event_t*, void*)`) reads the chip and publishes with `sys_io_publish` / `sys_power_publish` / `sys_hbridge_publish`, `hops = SYS_EVENT_CAUSED_BY(event)`. Teardown: `sys_event_unsubscribe(ctx->intr_sub, false)`. No route / action / callback fields in the adapter (SYS_EVENT.MD).
     - On `fail:`, call `SYS_DEV_INSTALL_FAIL(err, id, out, device_uninstall, ctx)`.
   - **uninstall** doubles as rollback:
     - Gate each step on `IF_SYS_DEV_STEP_DONE`.
     - Accumulate errors with `SYS_DEV_TEARDOWN_STEP` and never return early.
     - Always free the hw handle and ctx.
   - Implement all 7 ops. **`suspend` must leave every output safe** (off / coast / high-Z), because it's the fault path. While frozen, output writes are deferred and input reads return the snapshot (PCA9685 pattern; freeze is under review, §5.2).
   - `d_<chip>_create` is just `SYS_DEVICE_CREATE(&s_<chip>_class, cfg)`.
4. **Owner:** add `X(OWNER_DEVICE_<CHIP>, 0xD0xx, "...")` to `devices/devices/errors/devices_owners.h`.
5. **Aggregate:** include `device_<chip>.h` in `devices.h`.
6. **CMake:** `CMakeLists.txt` for the component, and add `device_<chip>` to `components/codecs/CMakeLists.txt` `REQUIRES`.
7. **Install packet:** create `codecs/decoders/device/dec_device_<chip>.h`:
   - Next free header byte in class 0x01 (0x40–0x47 used, **0x48** is next; header bytes are per class, so `vm_exec` using 0x48 in class 0x04 doesn't conflict).
   - `__packed` struct with `@required` / `@min` / `@max` / `@group` / `@role` field annotations.
   - A decoder that builds the cfg (`pin_ref_from_wire` for pin refs) and calls `d_<chip>_create`.
   - `SYS_CONTRACTS_DEVICE_<CHIP>_PACKET_LIST(X)`.
8. **Annotations** in that header (after the includes, before the packet): `//@id`, `//@version`, `//@title`, `//@description`, `//@protocol`, `//@tags`, `//@contract-provider $SYS_DEVICE_CONTRACT_…`, `//@self-property`, and one `//@contract packet_… @alias …` block per supported op. Every `@required` field of the generic packet needs an `@param`. Descriptions are user-facing.
9. **Register** the list in `dec_sys_device_install.h`.
10. **Regenerate:** `python data-structures/auto-annotations/device/generate-devices.py components/codecs/decoders data-structures/devices`.
11. **Onboard chip only:** add `DEVICE_ID_<CHIP>` to `runit_board_defs.h` and a create call to `runit_board_cfg.c`. Update [HARDWARE.md](../runit-root/HARDWARE.md).
12. **Build.** New Kconfig options need a reconfigure ([build.md](build.md)). Update `SYS_DEVICE.MD` if you changed `sys_device`, and update PROGRESS.md.

## 3. Rules

- **Device ID order = dependency order.** A device's `sys_io_pin_ref_t` fields always point at a **lower** device ID. Teardown sweeps go high→low and bring-up sweeps go low→high (SYS_DEVICE.MD). Onboard IDs: 0–4 IO/expanders/ADC/PWM/DAC, 10–13 power chips (HARDWARE.md §3).
- **One public function per device** (`d_<chip>_create`). All runtime access goes through contracts by `device_id`. If a chip needs an operation no contract has, extend the contract (in `sys_io` / `sys_power` / `sys_hbridge`) instead of exporting a device-specific API.
- **The driver/adapter split is strict:** register and bus logic stays in the driver, and system binding (contracts, errors, pins, freeze) stays in the adapter.
- **Validate once** ([conventions.md](conventions.md) §4). `d_<chip>_create` checks `cfg`. `install` receives the manager's own copy, so it doesn't need to re-check it. Internal helpers trust their caller.
- **Onboard devices** (on the PCB) are installed in `runit_board_devices_init` with `RUNIT_BOARD_DEVICE(id, d_<chip>_create(...))`, which marks them onboard. Users (packets, recorded actions) go through `sys_device_user_uninstall[_all]` and can't uninstall or replace them. Any other restriction is app policy. Keep `//@STATIC_DEVICE` on their ID so the app knows them.
- **Locked pins:** pins an adapter depends on (OE, RST, EN, INT) are locked after setup (`SYS_DEV_INSTALL_STEP(sys_io_lock_pin(pin), …)`) and unlocked in uninstall (`SYS_DEV_TEARDOWN_STEP`). Drive a locked pin later with `sys_io_set_locked_level(pin, level)`, which always restores the lock.
- **Annotations (`//@`) stay** ([conventions.md](conventions.md) §6). When an adapter gains or loses a contract op, update the matching `//@contract` block and regenerate.

## 4. Current state (review 2026-09-22)

| Device | Contracts | Install helpers + probe | freeze/sync | Decoder + JSON | Notes |
|---|---|---|---|---|---|
| gpio_esp | IO | own install (no I2C) | ✅ | ✅ 0x40 | Suspend = freeze alias: outputs not made safe on fault (§5.1), PWM keeps running too. PWM through LEDC (`esp_pwm.c`): channel per pin, timer per frequency (Kconfig `CONFIG_DEVICE_GPIO_ESP_PWM_TIMER_MASK` / `_CHANNEL_MASK` choose which LEDC timers / channels it may use; default all), duty 0..4096. Nothing else uses LEDC today; a new LEDC user must clear its timers / channels from the masks and use the APB clock (S3 timers share one clock source) |
| pca9685 | IO | ✅ | ✅ | ✅ 0x41 | Reference adapter. Orphaned half-finished comment ("Test implementation for the sys_device error-handling scheme…") above `device_install` |
| tca6424a | IO | ✅ | ✅ | ✅ 0x42 | Exports `d_tca6424a_new` / `_delete` besides `_create` |
| tps55289 | POWER_VREG | ✅, no `sys_i2c_device_present` | ⛔ **missing** | ✅ 0x43 | Suspend is safe (output off + EN low); freeze skipped (§5.2) |
| ina3221 | POWER_MONITOR | ✅ | ✅ | ✅ 0x44 | |
| ap33772s | VREG + USB_PD + MONITOR | ✅ | ✅ | ✅ 0x45 | Driver creates its own task dynamically (hard-coded 3072 B / prio 5) |
| dac53202 | IO | ✅ | ✅ | ✅ 0x46 | Not created on the board yet (needed for DRV8962 VREF) |
| ads7128 | IO | ✅ | ✅ | ✅ 0x47 | ADC-only; digital-input mode planned (PROGRESS) |
| drv8962 | HBRIDGE | ✅ install step + rollback (uninstall stops the chip and resets every dependency pin) | ✅ | ⛔ **no decoder, no JSON, not in `codecs` REQUIRES** | Pin-controlled chip: its driver works through `sys_io` and returns `err_h` (accepted exception). Dependency pins aren't locked because the driver writes its IN pins continuously. Not in the board config; 2nd chip is driven by PCA9685 CH 8–15 |

Cross-cutting:
- **Naming:** see [conventions.md](conventions.md) §5.2 (`*_handle_t`, `_<chip>_data_t`, `ads_` / `tca_` / `esp_` prefixes, INA3221 enums).

## 5. Design decisions

### 5.1 Faults → suspend
- **Suspend is the fault safe state.** The error policy, the system hook and `runit_enter_safe_state` suspend (never freeze). State becomes `SUSPENDED`, so new writes are **rejected** (`ERR_DEV_SUSPENDED`), not queued; the "resume" action (`resume_all` + `sync_all`) brings devices back.
- Each adapter's `suspend` leaves its outputs safe:

  | Device | Suspend action |
  |---|---|
  | PCA9685 | OE high + sleep |
  | TCA6424A | Held in reset |
  | TPS55289 | Output off + EN low (EN goes low even if the I2C disable fails) |
  | AP33772S | Output off |
  | DAC53202 | Powered down |
  | INA3221 | Powered down |
  | DRV8962 | All bridges coast |
  | ADS7128 | Nothing to do (input only) |

- **Open:** `device_gpio_esp`'s suspend is an alias of its freeze, so ESP GPIO outputs keep their level. It needs a real safe suspend. Careful: TCA6424A's RST is an ESP pin, and the reverse sweep suspends GPIO ESP (ID 0) last.

### 5.2 Freeze — kept for now, to be reconsidered (TODO)

Freeze is a process image: an input snapshot plus deferred output writes; `sync` flushes them. The "freeze" action and packets 0x16–0x19 use it.

Open question before reusing it (for example for VM pass boundaries): **write → read-back sequences** (write a config or trigger a conversion, then read the response) can never complete while frozen: the write waits for `sync`, the read returns the old snapshot. Options: exempt some ops (per contract op or a "transactional" flag), a per-device flush-and-relatch mid-pass, don't freeze such devices, or drop freeze.

Also open: coverage gaps (TPS55289 and DRV8962 have no freeze; AP33772S doesn't defer its VREG writes), and INA3221 sets `is_frozen` before its snapshot reads.

### 5.3 `const` vtables, per-instance pin locks
- Contract vtables are `static const` (flash); `sys_device_class_t.contracts[]` is `const void*` (no casts).
- IO pin locks are per instance (`sys_device_t.io_locked_pins`), so two chips of the same type don't share locks and locks disappear with the instance.

### 5.4 Device ID on every device error
- Contract calls are wrapped by the dispatchers (`SYS_DEV_DISPATCH`, `SYS_IO_DISPATCH`, `sys_power`, `sys_hbridge`) as `ERR_DEV_DEP_FAILED(dev_id)`; install failures as `ERR_DEV_INSTALL_FAILED(dev_id)`; lifecycle ops in `sys_device.c`.
- `*_all` sweeps are best effort: every device is attempted, each failure wrapped with its ID, the first returned, the rest sent to diagnostics.
- Adapters use `SYS_DEV_CHECK_DRIVER_CALL` / dispatchers and never create device errors that bypass `sys_device`. An asynchronous device task that raises errors itself uses `SE_WRAP_DEV_ERR(err, SYS_DEV_GET_ID(ctx))` before pushing.

### 5.5 Error context
- Drivers return `esp_err_t`; adapters add the context. `SYS_DEV_CHECK_DRIVER_CALL` / `SYS_DEV_INSTALL_STEP` produce `ERR_DEV_DRIVER_FAILED {dev_id, line}` / `ERR_DEV_INSTALL_STEP_FAILED {line}`; `SYS_DEV_DRIVER_ERR` builds one without returning. A typical chain: `ERR_DEV_DEP_FAILED(dev)` → `ERR_DEV_DRIVER_FAILED(dev, adapter line)` → `ERR_ESP_ERR(code)`.
- `SYS_DEV_INSTALL_FAIL` doesn't add `ERR_DEV_INSTALL_FAILED`; the manager adds it once.
- Allocation failures are `ERR_BASE_NO_MEM`.
- Known unchecked `esp_err_t` drops (the compiler can't see them): PCA9685 sleep (uninstall/suspend/resume), INA3221 alert current reads (events are still published for every channel), GPIO ESP pin reset / interrupt disable / `esp_adc_start`, ADC calibration in the background task (keeps the last good reading).
