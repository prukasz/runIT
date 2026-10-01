# Devices: class, contracts, create frames

API reference: [components/system/sys_device/SYS_DEVICE.MD](../../../components/system/sys_device/SYS_DEVICE.MD) (registry, lifecycle, macros). Board wiring and device IDs: [HARDWARE.md](../runit-root/HARDWARE.md). Annotation grammar: `data-structures/auto-annotations/device/device-annotations.md`.

## 1. Anatomy of a device

One device = one ESP-IDF component `components/devices/device_<chip>/`. It is picked up automatically through `EXTRA_COMPONENT_DIRS "components/devices"`.

```
components/devices/device_<chip>/
├── CMakeLists.txt          SRCS device_<chip>.c; REQUIRES sys_errors sys_io sys_device (+ sys_i2c / sys_power / sys_hbridge as used)
├── include/device_<chip>.h PUBLIC: <CHIP>_TYPE_ID, d_<chip>_cfg_t (the create frame), g_<chip>_class, annotations
└── device_<chip>.c         PRIVATE, the whole device: chip access (err_h, over sys_i2c_*), contract functions, lifecycle ops, the class
```

Plus, outside the component:

| File | Purpose |
|---|---|
| `components/devices/devices/include/devices.h` | Includes every `device_<chip>.h` |
| `components/devices/devices/devices.c` | `SE_TRY(sys_device_register_class(&g_<chip>_class))` in `devices_register_classes()` |
| `components/codecs/CMakeLists.txt` | `device_<chip>` in `REQUIRES` |
| `data-structures/devices/device_<chip>.generated.json` | Generated from the annotations in `device_<chip>.h`. Never edit by hand |
| `components/runit/runit_board_defs.h` / `runit_board_cfg.c` | Only for **onboard** chips: `DEVICE_ID_<CHIP>` (`//@STATIC_DEVICE`) and `SYS_DEVICE_CREATE(&g_<chip>_class, &(d_<chip>_cfg_t){...})` in `runit_board_devices_init()` |

### Layers and flow

```
app / VM ──[0x01][0x00][type_id][cfg]──► sys_device_route ──► sys_device_create(cls, cfg)
                                                                  │ size + pin order, then sys_device_install
                                                                  ▼
sys_device registry [device_id] ── cls->ops.install ──► device_<chip>.c ──► sys_i2c / sys_io
                                                            ▲
sys_io_* / sys_power_* / sys_hbridge_* (by device_id) ──────┘ contract vtable call
```

- **Contracts** (`sys_device_contract_type_e`): `IO`, `POWER_VREG`, `POWER_MONITOR`, `POWER_USB_PD`, `HBRIDGE`. The vtable types are defined by `sys_io`, `sys_power` and `sys_hbridge`. A device may provide several contracts (AP33772S: VREG + USB_PD + MONITOR), or none (the servo has operations of its own).
- **Class vs instance:** each device declares one public `const sys_device_class_t g_<chip>_class` (type id, cfg size, pin refs, name, `contracts[]`, `ops`, own operations). `sys_device` owns the instance: the registry slot, state and error-handling settings. It keeps no copy of the cfg: `install` receives it for the call only and copies what it needs into the device's own state.
- **Lifecycle ops:** `install`, `uninstall`, `reset`, `suspend`, `resume`. A missing op makes the manager return `ERR_BASE_NOT_SUPPORTED`. **`suspend` is the fault safe state** (critical faults call `sys_device_suspend_all()`), and `resume` brings the device back. Freeze / sync were removed (§5.2).

## 2. Adding a new device: checklist

Reference: `device_pca9685/device_pca9685.c` (contract), `device_servo` (own operations), `device_tca6424a` (interrupt pins).

1. **Public header** `include/device_<chip>.h`:
   - `#define <CHIP>_TYPE_ID 0x..` (the byte after `0x00` in a create frame; next free one).
   - The `//#device` + `//#contract` annotation records (`@type-id <CHIP>_TYPE_ID`, `@contract-provider ...`), then `typedef struct __packed { ... } d_<chip>_cfg_t;` with **`uint8_t device_id` first**. It is the create frame: field annotations only where the app shows the field (`@alias`, `@one-of`, `@min` / `@max`, `@unit`, `@note`). A pin on another device is one `sys_io_pin_ref_t <name>_pin` field (`@alias`, `@note`, `@modes`, `@default-mode`); document that "unused" must be written `SYS_IO_PIN_NONE_INIT`, because a zero-filled field means device 0 / pin 0.
   - `extern const sys_device_class_t g_<chip>_class;` and nothing else. Grammar: `data-structures/auto-annotations/device/device-annotations.md`, template: `device-template.txt`.
2. **Device file** `device_<chip>.c` (one file; no driver/adapter split):
   - `#define OWNER OWNER_DEVICE` (one owner for every device file; the device id in the error says which device). **No `ESP_LOG*`**, no `TAG`: context goes into errors, and `sys_device` logs installs.
   - A context struct whose first member is `sys_device_base_t base`, then `sys_i2c_dev_t i2c`, the pins the device keeps as `sys_io_pin_ref_t` (copied from the cfg, same type), chip state. The cfg is not kept.
   - Chip access: `static err_h chip_*(ctx, ...)` over `sys_i2c_reg_*` / `sys_i2c_write[_read]`. The dispatchers add the device id; an event listener (no dispatcher) wraps its own error with `SE_WRAP_DEV_ERR`.
   - Contract functions start with `SYS_DEV_CTX_FROM(ctx_t, ctx, handle)`. Validate pins with `VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, MASK)`, and every value the chip would reject with `SE_CHECK_IN_RANGE` (value, min and max go into the error), never by clamping.
   - `static const` contract vtables (unsupported ops = `NULL`); never runtime state in a vtable. Own operations: a `static const sys_device_op_t` table (`SYS_DEVICE_OP`, `SYS_DEVICE_OP_RO`) and `SYS_DEVICE_OPS(table)` in the class.
   - **install:** chip-specific range checks first (they come wrapped in `ERR_DEV_INSTALL_FAILED`), then calloc the context, then each step through `SYS_DEV_INSTALL_STEP(expr, "what")` followed by `SYS_DEV_STEP_DONE(ctx, STEP_BIT)`:
     - `sys_i2c_dev_init` + `sys_i2c_dev_add` (probes the chip).
     - Dependency pins: `sys_io_set_mode`, then `sys_io_lock_pin`.
     - An alert / IRQ pin: `sys_io_subscribe_pin(pin, device_event_handler, ctx, &ctx->intr_sub)` (step bit), `sys_io_configure_intr(pin, {.mode = FALLING_EDGE})`, lock. The handler (`err_h (const sys_event_t*, void*)`) reads the chip and publishes with `sys_io_publish` / `sys_power_publish` / `sys_hbridge_publish`, `hops = SYS_EVENT_CAUSED_BY(event)`. Teardown: `sys_event_unsubscribe(ctx->intr_sub, false)`.
     - On `fail:`, call `SYS_DEV_INSTALL_FAIL(err, out, device_uninstall, ctx)`.
   - **uninstall** doubles as rollback: gate each step on `IF_SYS_DEV_STEP_DONE`, accumulate errors with `SYS_DEV_TEARDOWN_STEP`, never return early, always free the context.
   - Implement the 5 ops. **`suspend` must leave every output safe** (off / coast / high-Z), because it's the fault path. A suspended device rejects writes (`ERR_DEV_SUSPENDED`).
   - The class: `.type_id = <CHIP>_TYPE_ID`, `.cfg_size = sizeof(d_<chip>_cfg_t)`, `SYS_DEVICE_PINS(table)` with `static const uint8_t table[] = {offsetof(d_<chip>_cfg_t, <pin>), ...}` (every `sys_io_pin_ref_t` field; the generator fails when the table and the cfg disagree), `.name`, `.contracts = {[SYS_DEVICE_CONTRACT_X] = &s_vtable}`, `.ops`. `sys_device_create` checks the size and the pin order, so the device writes neither.
3. **Aggregate:** include `device_<chip>.h` in `devices.h`.
4. **CMake:** `CMakeLists.txt` for the component (`SRCS "device_<chip>.c"`, `REQUIRES sys_errors sys_io sys_device` + `sys_i2c` / `sys_power` / `sys_hbridge` as used), and add `device_<chip>` to `components/codecs/CMakeLists.txt` `REQUIRES`.
5. **Register:** one line in `devices_register_classes()` (`components/devices/devices/devices.c`).
6. **Regenerate:** `python data-structures/auto-annotations/device/generate-devices.py components/codecs/decoders data-structures/devices`; `test_generate_devices.py` after grammar changes.
7. **Onboard chip only:** add `DEVICE_ID_<CHIP>` to `runit_board_defs.h` and a `SYS_DEVICE_CREATE` call to `runit_board_cfg.c`. Update [HARDWARE.md](../runit-root/HARDWARE.md).
8. **Build.** New Kconfig options need a reconfigure ([build.md](build.md)). Update `SYS_DEVICE.MD` if you changed `sys_device`, and update PROGRESS.md.

## 3. Rules

- **Device ID order = dependency order.** A device's `sys_io_pin_ref_t` fields always point at a **lower** device ID. Teardown sweeps go high→low and bring-up sweeps go low→high (SYS_DEVICE.MD). Onboard IDs: 0–4 IO/expanders/ADC/PWM/DAC, 10–13 power chips (HARDWARE.md §3). User installs enforce it: a class device through `sys_device_create` (from `SYS_DEVICE_PINS`), a not migrated one in its install decoder via `PIN_REFS_BELOW(cfg.device_id, pins…)` (`dec_device_common.h`) → `ERR_DEV_PIN_ORDER` (dev_id, pin_dev_id, pin). The app installs user devices in ID order and refuses a pin on a same / higher ID.
- **One public symbol per device**: `g_<chip>_class`. All runtime access goes through contracts by `device_id`. If a chip needs an operation no contract has, extend the contract (in `sys_io` / `sys_power` / `sys_hbridge`) instead of exporting a device-specific API.
- **One file per device, no driver/adapter bridge** (class shape): chip access is a block of `static err_h chip_*(ctx, ...)` functions at the top of `device_<chip>.c` over `sys_i2c_dev_t` + `sys_i2c_reg_*` (the dispatchers add the device id; an event listener wraps its own error with `SE_WRAP_DEV_ERR`). Hardware subsystems that are more than register access (ESP GPIO: `esp_pwm.c` LEDC, `esp_adc_config.c` ADC task) stay separate private modules. Devices not migrated yet still have the `driver_<chip>` / `adapter_<chip>` split (§2 steps 1 and 3 describe it).
- **Validate once** ([conventions.md](conventions.md) §4). `sys_device_create` checks the cfg size and pin order; `install` checks chip-specific ranges once, and internal helpers trust it. Internal helpers trust their caller.
- **Onboard devices** (on the PCB) are installed in `runit_board_devices_init` with `SE_TRY(SYS_DEVICE_CREATE_ONBOARD(&g_<chip>_class, &(d_<chip>_cfg_t){.device_id = DEVICE_ID_<CHIP>, ...}))`, which marks them onboard (the generator of `board.generated.json` reads exactly this form: `.device_id` first). Users (packets, recorded actions) go through `sys_device_user_uninstall[_all]` and can't uninstall or replace them. Any other restriction is app policy. Keep `//@STATIC_DEVICE` on their ID so the app knows them.
- **Locked pins:** pins a device depends on (OE, RST, EN, INT) are locked after setup (`SYS_DEV_INSTALL_STEP(sys_io_lock_pin(pin), …)`) and unlocked in uninstall (`SYS_DEV_TEARDOWN_STEP`). Drive a locked pin later with `sys_io_set_locked_level(pin, level)`, which always restores the lock.
- **Annotations stay** ([conventions.md](conventions.md) §6). When a device gains or loses a contract op, update the matching `//#contract` record and regenerate. Layout (2026-09-28): records continued by `//  @tag` lines, `a..b` ranges, `sys_io_pin_ref_t` pin fields, only tags the app reads. Grammar: `data-structures/auto-annotations/device/device-annotations.md`. Added 2026-09-27: `@param <field> @device-wide` (the field selects nothing: PCA9685 frequency pin, the app hides it and sends 0); `//@pwm-frequencies <value> [@count-bits]` (per-pin-frequency devices: how many frequencies at once, ESP GPIO = LEDC timers); contract `@min` / `@max` / `@default` resolve C defines and `CONFIG_*` (unresolved fails generation).
- **Range-check in the device, not by clamping.** PCA9685 frequency: `SE_CHECK_IN_RANGE(PCA9685_MIN_FREQUENCY_HZ 24, PCA9685_MAX_FREQUENCY_HZ 1526)` (it used to clamp, and >65535 wrapped).
- **Reversed shunts (INA3221 `inverted_mask`):** alerts stay possible (signed limit compare): `set_inverted_alert` writes −threshold, makes that alert transparent (no latch), moves its pin to the rising edge and remembers the channel per pin; the release edge publishes the event. One reversed channel per alert pin (`ERR_IO_PIN_ALREADY_IN_USE`); a device reset disarms them. The normal-polarity path is unchanged (flag loops only skip reversed channels).

## 4. Current state (2026-10-01)

Every device is in the class shape: one file `device_<chip>.c`, a public header with the create frame, no decoder and no install packet. Built, not yet run on the board after the refactor.

| Device | Type | Contracts | Notes |
|---|---|---|---|
| gpio_esp | `0x40` | IO | Suspend makes the user's outputs safe (PWM duty 0, push-pull low, open-drain released; locked pins are left to their owner) and resume restores them (§5.1). PWM through LEDC (`esp_pwm.c`): channel per pin, timer per frequency (Kconfig `CONFIG_DEVICE_GPIO_ESP_PWM_TIMER_MASK` / `_CHANNEL_MASK` choose which LEDC timers / channels it may use; default all), duty 0..4096. Nothing else uses LEDC today; a new LEDC user must clear its timers / channels from the masks and use the APB clock (S3 timers share one clock source). `esp_pwm.c` and `esp_adc_config.c` stay separate files: they are subsystems, not register access |
| pca9685 | `0x41` | IO | Reference device |
| tca6424a | `0x42` | IO | Interrupt pin + RST pin; the listener publishes the edges of the armed pins |
| tps55289 | `0x43` | POWER_VREG | Suspend is safe (output off + EN low). External feedback (`VOUT_FS` bit 7) is refused |
| ina3221 | `0x44` | POWER_MONITOR | `inverted_mask` (reversed shunts) is a wire field |
| ap33772s | `0x45` | VREG + USB_PD + MONITOR | Own AVS keep-alive task (3072 B / prio 5, started after the PDOs are read). The INT pin is claimed but no listener is subscribed |
| dac53202 | `0x46` | IO | A voltage above 3300 mV is refused (it used to be clamped) |
| ads7128 | `0x47` | IO | ADC-only; digital-input mode planned (PROGRESS). Sampling is fixed (no oversampling, low-power oscillator, open-drain active-low ALERT) |
| servo | `0x48` | none | Operations only: `0x80` home, `0x81` set offset, `0x82` set angle, `0x83` get pulse (read-only); drives any PWM pin through the IO contract |
| drv8962 | `0x49` | HBRIDGE (set_drive, brake, coast, get_current) | Chip specifics are operations: `0x80` set_current_limit (VREF), `0x81` clear_fault (nSLEEP pulse), `0x82` get_fault (read-only). Topology (2 full / 4 half bridges) is in the cfg, so the contract has no `set_mode`. Pins are flat cfg fields (`in1_pin` .. `vref_pin`; no pin ref arrays). Rollback: coast, unsubscribe nFAULT, reset every dependency pin |

Cross-cutting:
- **Naming:** see [conventions.md](conventions.md) §5.2.
- **Operations** of a device (`0x80`..`0xFF`; examples: `device_servo.c`, `device_drv8962.c`): a table of `SYS_DEVICE_OP` / `SYS_DEVICE_OP_RO` rows in the class; use them for what is specific to one chip. A function that two chips need becomes a contract function.

## 5. Design decisions

### 5.1 Faults → suspend
- **Suspend is the fault safe state.** The error policy, the system hook and `runit_enter_safe_state` suspend. State becomes `SUSPENDED`, so new writes are **rejected** (`ERR_DEV_SUSPENDED`), not queued; the "resume" action (`resume_all`) brings devices back.
- Each device's `suspend` leaves its outputs safe:

  | Device | Suspend action |
  |---|---|
  | PCA9685 | OE high + sleep |
  | TCA6424A | Held in reset |
  | TPS55289 | Output off + EN low (EN goes low even if the I2C disable fails) |
  | AP33772S | Nothing: its output is the board's own supply, switching it off browns the ESP out (2026-10-01) |
  | DAC53202 | Powered down |
  | INA3221 | Powered down |
  | DRV8962 | All bridges coast |
  | GPIO ESP | Unlocked outputs: PWM duty 0, push-pull low, open-drain released (restored on resume) |
  | ADS7128 | Nothing to do (input only) |

- **GPIO ESP:** pins locked by another device are skipped (their owner makes them safe in its own suspend). The reverse sweep suspends GPIO ESP (ID 0) last, so TCA6424A's RST (an ESP pin) is still driven by its owner first. A suspend that fails half way still leaves the device suspended (`sys_device` sets the state on failure), so resume restores the pins already made safe.

### 5.2 Freeze removed (2026-10-01)

Freeze / sync (a process image: input snapshot plus deferred output writes) is gone from `sys_device`, every device, the packets `0x16`–`0x19` and the "freeze" static action (Kconfig `SYS_ACTION_ID_FREEZE`; ids 3–6 keep their numbers). Write → read-back sequences can't complete while frozen, and coverage was uneven (TPS55289 / DRV8962 had none). A consistent image for a VM pass belongs in the VM (read inputs at pass start, write outputs at pass end), not in the devices. The app (`DeviceDetails.tsx`, `deviceCatalog.ts`) and the generated contracts / enums JSON still list freeze until they are regenerated.

### 5.3 `const` vtables, per-instance pin locks
- Contract vtables are `static const` (flash); `sys_device_class_t.contracts[]` is `const void*` (no casts).
- IO pin locks are per instance (`sys_device_t.io_locked_pins`), so two chips of the same type don't share locks and locks disappear with the instance.

### 5.4 Device ID on every device error
- Contract calls are wrapped by the dispatchers (`SYS_DEV_DISPATCH`, `SYS_IO_DISPATCH`, `sys_power`, `sys_hbridge`) as `ERR_DEV_DEP_FAILED(dev_id)`; install failures as `ERR_DEV_INSTALL_FAILED(dev_id)`; lifecycle ops in `sys_device.c`.
- `*_all` sweeps are best effort: every device is attempted, each failure wrapped with its ID, the first returned, the rest sent to diagnostics.
- Devices never create device errors that bypass `sys_device`: the dispatchers add the device id to every contract call. An asynchronous device task that raises errors itself uses `SE_WRAP_DEV_ERR(err, SYS_DEV_GET_ID(ctx))` before pushing.

### 5.5 Error context
- Chip access returns `err_h` (`sys_i2c_*` already converts the ESP code to `ERR_ESP_ERR`; an ESP-IDF call made directly uses `SE_CONVERT_ESP` / `SE_TRY_ESP`). The dispatcher wraps the chain with `ERR_DEV_DEP_FAILED(dev_id)`, `SYS_DEV_INSTALL_STEP` adds `ERR_DEV_INSTALL_STEP_FAILED {line}`, and `ERR_DEV_INSTALL_FAILED` comes from the manager. A typical chain: `ERR_DEV_DEP_FAILED(dev)` -> `ERR_ESP_ERR(code)`.
- `SYS_DEV_INSTALL_FAIL` doesn't add `ERR_DEV_INSTALL_FAILED`; the manager adds it once.
- Allocation failures are `ERR_BASE_NO_MEM`.
- Known unchecked `esp_err_t` drops (the compiler can't see them): GPIO ESP: the ADC calibration in the background task (keeps the last good reading) and `esp_pwm_release` on the failure path of `set_mode` (it only frees the channel there).
