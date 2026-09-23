# Firmware coding conventions

Rules for all code under `components/` and `main/`. Error handling has its own file: [errors.md](errors.md).

## 1. RTOS objects — static allocation (`components/utils/utils.h`)

**Rule:** if an RTOS object exists from boot to shutdown (system components: module mutexes, queues, ringbuffers, tasks), define it with the `R_*_DEFINE` macros. Don't create it at runtime.

Why:
- The memory is in `.bss`, so `idf.py size` / `idf.py size-components` shows the real RAM cost per component.
- There's no heap, no fragmentation, and no "creation failed" error path.

Use dynamic creation (`xQueueCreate`, `xRingbufferCreate`, ...) only for objects whose count or lifetime is decided at runtime. Examples: per-instance device driver resources, `sys_buffers`, data connectors created from packets.

| Macro | Creates | Notes |
|---|---|---|
| `R_MUTEX_DEFINE(name)` / `R_RECURSIVE_MUTEX_DEFINE` | mutex | Lock with `R_MUTEX_LOCK(h, ticks)`, which returns true on success |
| `R_BINARY_SEM_DEFINE`, `R_COUNTING_SEM_DEFINE(name, max, init)` | semaphores | `R_SEM_GIVE_ISR` yields if needed |
| `R_QUEUE_DEFINE(name, len, item_size)` | queue | `R_QUEUE_SEND/RECEIVE/PEEK`, `_ISR` variant |
| `R_RINGBUFFER_DEFINE(name, size, type)` | ESP ringbuffer | size must be a multiple of 4 |
| `R_STREAM_BUFFER_DEFINE`, `R_MESSAGE_BUFFER_DEFINE` | stream/message buffer | usable capacity is **size − 1** (see fix list) |
| `R_EVENT_GROUP_DEFINE`, `R_TIMER_DEFINE` | event group, software timer | `R_EVENT_*`, `R_TIMER_*` helpers |
| `R_TASK_DEFINE(name, stack)` + `R_TASK_START[_ON_CORE]` | task (stack + TCB static) | stack size is in **bytes** (`StackType_t` is `uint8_t` on ESP-IDF). The task starts only when `R_TASK_START` is called |

How the macros work:
- `R_*_DEFINE` (except tasks) builds the object in an `__attribute__((constructor))` function. ESP-IDF runs constructors in `start_cpu0_default()` after core/heap init and before the scheduler starts, so init functions never have to create these objects themselves.
- Timing: the handle is `NULL` until its own constructor has run, and constructors in different `.c` files run in **unspecified order**. So a constructor must never use an `R_*` handle defined in another file. Don't use constructors for anything else (registration goes through runit, architecture.md §4.1).
- Tasks: start them in the component's init function behind a guard, `if (h == NULL) R_TASK_START(...)`. Starting the same task twice re-initializes the running task's TCB.
- Sizes and priorities: take stack sizes, priorities and depths from Kconfig (`CONFIG_<COMPONENT>_*`), not literals (see §2).

### Open `utils.h` fixes

The macros work (the firmware boots on hardware). Fixes worth doing:

| # | Issue | Risk | Fix |
|---|---|---|---|
| 1 | The handle has **external linkage** (`QueueHandle_t name = NULL;`), and `static` can't be put in front of the macro | Two files using the same handle name (for example `s_mutex`) fail at link time. Every handle is a global symbol, even the ones meant to be file-private | Emit `static` handles by default. Add an `R_*_DEFINE_SHARED` (or a scope argument) for the 3 handles that really are shared: `gpio_mutex`, `sys_ble_mutex`, `sys_ble_tx_sem` |
| 2 | `R_TASK_START` doesn't check whether the task is already running | Calling it a second time corrupts a running task. Right now callers guard it by hand (`sys_event`, `sys_interface`, …) | Put the `if (name == NULL)` guard inside the macro |
| 3 | `R_TASK_DEFINE(name, stack_words)`: the parameter name is wrong | Misleading: on ESP-IDF the value is bytes. Current values (4096, 6144) are correct as bytes | Rename to `stack_bytes` |
| 4 | Static stream/message buffers hold `size − 1` bytes (the dynamic API adds 1 internally, the static one doesn't) | Off-by-one capacity. Nothing uses these macros yet | Allocate `size + 1` storage and pass `size + 1` |
| 5 | `R_NOTIFY_AWAIT` uses `ULONG_MAX` without including `<limits.h>` | Compile error when first used. Unused today | Use `UINT32_MAX` (`<stdint.h>` is already included) |
| 6 | `IS_ENABLED` is also defined by IDF's Zephyr/NimBLE-mesh headers | Redefinition if both end up in one file | Rename it to `R_IS_ENABLED` |
| 7 | `R_RINGBUFFER_DEFINE` doesn't check that the size is 4-aligned | A misaligned size asserts at runtime | Add `_Static_assert((buffer_size) % 4 == 0, ...)` |
| 8 | `R_QUEUE_SEND_ISR` ignores the result when the queue is full | Items are dropped silently | Return the send result from the macro |
| 9 | `LL_*` list macros have Polish comments and sit outside `extern "C"` | Cosmetic | Translate the comments and move them inside |

Places not yet following the rule:
- `vm_exec.c` → `R_TASK_DEFINE(vm_exec_task_h, 6144)` and `esp_adc_config.c` → `R_TASK_DEFINE(adc_processing_task, 4096)` hard-code their stack sizes. Move them to Kconfig.
- `driver_ap33772s.c` creates its service task with `xTaskCreate(..., 3072, ..., 5, ...)`: dynamic, with a hard-coded stack and priority. Decide whether it is per-instance (dynamic is fine, but take the values from Kconfig) or a singleton (make it static).

## 2. Constants and configuration

- **Internal constants** that belong to one file (register addresses, bit masks, protocol offsets, internal limits) are `#define`d at the top of that `.c` file. They don't go in a public header unless another component needs them.
- **Tunables**: anything that could reasonably change (buffer sizes, queue depths, stack sizes, task priorities/cores, timeouts, counts, IDs) or be exposed to users goes in **Kconfig**, in the owning component's `Kconfig` under that component's menu. Use it as `CONFIG_<COMPONENT>_<NAME>`.
- After adding or changing Kconfig options, run `idf.py reconfigure`. If you changed a default, run `idf.py refresh-config --policy=kconfig` instead. See [build.md](build.md) §3. Commit the resulting `sdkconfig` change.
- **No fallback `#ifndef X` / `#define X` pairs** for values that come from Kconfig or another header. If the value is missing, the build must fail. (Currently none exist; keep it that way.) Include guards use `#pragma once`.
- For Kconfig bools used in a runtime `if`, use `IS_ENABLED(CONFIG_X)`, because disabled bools are simply not defined.

## 3. Changing code — no legacy leftovers

- When replacing an API, name, macro or constant, **remove the old one** and update every caller in the same change. Don't keep compatibility aliases, `#define OLD NEW`, deprecated wrappers or duplicate definitions "for legacy".
- The same applies to Kconfig options: rename or remove them outright (and reconfigure). Don't add a second option that shadows the old one.
- Remove dead code instead of commenting it out.
- **No versioning before release.** Wire formats, packets, stored settings and generated JSON change in place:
  - no version bytes or fields;
  - no "v2 / v3" labels in code or docs;
  - no code that accepts several formats.

  The firmware, the generated `data-structures/` JSON and the app are updated together. What catches a mismatch is the schema ID and the regenerated JSON, not a version number.

## 4. Validation happens once, at the boundary

- Check pointers, ranges and state **once**, where data enters: public API functions, contract operations, decoders of incoming packets. Use `SE_CHECK_NOT_NULL`, `SE_CHECK_IN_RANGE`, `SE_CHECK_HANDLE`.
- Internal helpers (`static` functions, private helpers called only after validation) **do not re-check** the same pointer or value. Their contract is "caller already checked"; say so in the helper's comment if it isn't obvious.
- Validate each new value where it first appears. For example, check a pointer obtained by lookup inside a helper right there.

## 5. Naming

### 5.1 Rules

- **snake_case only.** Words are separated with `_`: no camelCase, no PascalCase. Macros and constants are `UPPER_SNAKE`.
- **Type suffixes:**

  | Kind | Suffix | Example |
  |---|---|---|
  | function pointer / callback | `_f` | `sys_interface_handler_f`, `sys_event_handler_f` |
  | struct / union / plain typedef | `_t` | `sys_io_pin_ref_t`, `vm_obj_t` |
  | handle (opaque pointer to an instance) | `_h` | `err_h`, `vm_obj_h`, `vm_block_h` |
  | enum | `_e` | `se_level_e` |

- **Fixed-width integers from `<stdint.h>`** (`uint8_t`, `int32_t`, …) and `bool` from `<stdbool.h>`. Don't use `int`, `unsigned`, `long` or `short` for data. Accepted exceptions: `(unsigned long)` casts for `%lu` in printf, `_Generic` dispatch lists (`sys_error.h`), and API-mandated types (`esp_err_t`, `BaseType_t`, `size_t`).
- **Units in real notation** at the end of identifiers: `voltage_mV`, `current_mA`, `budget_mW`, `frequency_Hz`, `delay_us`, `period_ms` (not `_mv`, `_ma`, `_HZ`). All-caps macros and Kconfig keep caps (`…_LIMIT_MV`); ESP-IDF fields keep IDF's spelling (`freq_hz`). Measured outputs are signed (`int32_t* out_mV`).
- **Contracts:** `sys_<domain>[_<kind>]_contract_t`, members without a domain prefix, adapter instances `static const … s_<chip>_<kind>_contract`.
- **No leading underscore** on identifiers. Names starting with `_` at file scope are reserved in C; name private structs `<module>_data_t`, not `_<module>_data_t`.
- **Hierarchical names for anything exposed** (public headers, Kconfig, error codes), from general to specific:

  | Thing | Pattern | Example |
  |---|---|---|
  | System module API | `sys_<module>[_<object>]_<action>` | `sys_io_set_mode`, `sys_power_set_limits` |
  | VM API | `vm_<part>_<action>` | `vm_obj_get_items_cnt`, `vm_exec_stop`, `vm_loader_add_accessor` |
  | Device create (the only public device function) | `d_<chip>_create` + `d_<chip>_cfg_t` | `d_tps55289_create` |
  | Driver (internal to the device) | `<chip>_<action>`, handle `<chip>_h` | `ina3221_start` |
  | Feature | `feature_<name>_<action>` | `feature_servo_set_angle` |
  | Board / app | `runit_<area>_<action>` | `runit_board_devices_init` |
  | Codec | `dec_<class>_…` / `enc_<class>_…` | `enc_sys_errors_encode_chain` |
  | Kconfig | `CONFIG_<MODULE>_<NAME>` | `CONFIG_SYS_EVENT_TASK_STACK_SIZE` |
  | Error tag / owner | `ERR_<MODULE>_<WHAT>` / `OWNER_<MODULE>_<SUB>` | `ERR_IO_PIN_LOCKED`, `OWNER_SYS_IO_BASE` |

  The module segment matches the component folder name exactly (`sys_buffers_*`, not `sys_buff_*`).
- **Established exception:** the error API uses the prefix `SE_` (`SE_release`, `SE_TRY`). Keep it; don't invent similar short prefixes for other modules.
- **Function-pointer / callback typedefs end in `_f`**, with the usual hierarchical prefix, for example `sys_event_route_f` or `vm_block_f`.

### 5.2 Current deviations (open)

| Area | Deviation | Should be |
|---|---|---|
| All 8 device drivers | Handle types `ads_handle_t`, `ap33772s_handle_t`, `dac53202_handle_t`, `drv8962_handle_t`, `ina3221_handle_t`, `pca9685_handle_t`, `tca6424a_handle_t`, `tps55289_handle_t` | `<chip>_h` |
| Drivers | Private structs with a leading `_`: `_ap33772s_data_t`, `_dac53202_data_t`, `_ina3221_data_t`, `_pca9685_data_t`, `_tps55289_data_t` | `<chip>_data_t` |
| Driver prefixes | `ads_*` (ADS7128), `tca_*` / `tca_data_t` (TCA6424A), `esp_*` (gpio_esp) | `ads7128_*`, `tca6424a_*`, `gpio_esp_*` (`esp_*` also collides with the IDF namespace) |
| `device_tca6424a` | Exports `d_tca6424a_new` / `d_tca6424a_delete` besides `_create` | only `d_tca6424a_create` |
| `driver_ina3221.h` | Enums `ina3221_avg_t`, `ina3221_channel_t`, `ina3221_ct_t` | `_e` |
| `vm_block_edge.h` | Union `vm_edge_val_u` | `vm_block_edge_val_t` |
| `driver_ap33772s.h` | `REQMSG_Fields` (capitals); bit-fields declared `unsigned int` | snake_case `_t`; `uint32_t` bit-fields (GCC supports them) |
| `sys_actions` | `sys_actions_*` and `sys_action_*` mixed | `sys_actions_*` |
| `sys_buffers` | `sys_buff_*` | `sys_buffers_*` |
| `sys_errors` | Internal `se_log_*` next to `SE_*` | pick one case for the prefix |
| Public headers exporting generic names | `populate_svc_def` (ble), `convert_to_packet` (sys_interface), `slot_store` (VM), `vm_internal_*` in a public header | make them `static`/private, or give them a module prefix |
| `features/registry` | `feature_alloc`, `feature_get_by_id`, `feature_remove_*` (sounds like per-feature API) | `features_registry_*` or `feature_registry_*` |
| Function-pointer typedefs | `vm_block_fn` (+ verify), `feature_teardown_fn`, `runit_boot_step_fn`, `action_static_func_t` | `_f`: `vm_block_f`, `vm_block_verify_f`, `feature_teardown_f`, `runit_boot_step_f`, `sys_actions_static_f` (the last also fixes the prefix) |

Integer types are clean: the only non-`stdint` uses are the accepted exceptions above plus the AP33772S bit-fields.

## 6. Code annotations (`//@`, `//#ref-enum`)

- Comments starting with `//@` or `//#ref-enum` (for example `//@STATIC_DEVICE`, `//@contract`, `//@param`, and `@required` / `@arg` field metadata) are **machine-read** by the generators in `data-structures/auto-annotations/`. They produce the `*.generated.json` files the app uses. They currently appear in 22 files.
- **Never remove, reword or reformat them**, including during cleanup. When moving or renaming the declaration they describe, move the annotation with it and keep it next to the declaration.
- Grammar and rules: `data-structures/AGENTS.md` and the `*-annotations.md` files it links to. After changing annotated code, regenerate the JSON (commands in `data-structures/AGENTS.md`). Never edit `*.generated.json` by hand.

## 7. Module setup

- Every `.c` that raises errors defines `#define OWNER OWNER_<MODULE>` (see [errors.md](errors.md)). A file that logs defines `#define TAG "<module>"`. A file that uses `DBG()` aliases its component switch: `#define DBG_ENABLE CONFIG_DBG_ENABLE_<COMPONENT>`.
- Debug-only code goes in `DBG(...)`. It compiles to nothing unless `CONFIG_DBG_GLOBAL` or the component's `CONFIG_DBG_ENABLE_*` is on.
- File-private state is `static` and prefixed `s_`.

## 8. Comments and documentation

- Comment headers and macros in Doxygen style (`@brief`, `@param`, `@return`), with a minimal usage example for new APIs.
- **Never edit or rewrite a comment block the user wrote**, even if it looks stale after a code change. You may add your own comment, but keep it visually separate. Prefer one `/* ... */` block over many scattered `//` lines.
- When a component's `.h`/`.c` changes, update its `*.MD` doc (`SYS_IO.MD`, `SYS_DEVICE.MD`, …) in the same change, without being asked.
- If you find a component with no `.MD`, ask the user for its purpose, then write the doc.

## 9. Working style

- Don't make large architectural assumptions. When requirements are ambiguous, present options and ask.
- Don't guess the purpose of unknown registers, hardware logic or schemas. Ask, then record the answer (in HARDWARE.md, the component doc or this skill).
- Record user corrections and new rules in the matching skill file straight away.
