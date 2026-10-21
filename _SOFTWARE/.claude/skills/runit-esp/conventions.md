# Firmware coding conventions

Rules for all code under `runit-esp/components/` and `runit-esp/main/`. Error handling has its own file: [errors.md](errors.md). A component's `AGENTS.md` adds the rules for that component.

## 1. RTOS objects — static allocation (`runit-esp/components/core/utils/utils.h`)

**Rule:** an RTOS object that exists from boot to shutdown (module mutexes, queues, ring buffers, tasks) is defined with the `U_*_DEFINE` macros, not created at runtime.

Why:
- The memory is in `.bss`, so `idf.py size` / `idf.py size-components` shows the real RAM cost per component.
- No heap, no fragmentation, no "creation failed" error path.

Use dynamic creation only for objects whose count or lifetime is decided at runtime (per-instance driver resources, user connectors).

| Macro | Creates | Notes |
|---|---|---|
| `U_MUTEX_DEFINE(name)` / `U_RECURSIVE_MUTEX_DEFINE` | mutex | `U_MUTEX_LOCK(h, ticks)` returns true on success |
| `U_BINARY_SEM_DEFINE`, `U_COUNTING_SEM_DEFINE(name, max, init)` | semaphores | `U_SEM_GIVE_ISR` yields if needed |
| `U_QUEUE_DEFINE(name, len, item_size)` | queue | `U_QUEUE_SEND/RECEIVE/PEEK`, `_ISR` variant |
| `U_RINGBUFFER_DEFINE(name, size, type)` | ESP ring buffer | size must be a multiple of 4 |
| `U_STREAM_BUFFER_DEFINE`, `U_MESSAGE_BUFFER_DEFINE` | stream / message buffer | usable capacity is **size − 1** (open fix 4) |
| `U_EVENT_GROUP_DEFINE`, `U_TIMER_DEFINE`, `U_SPINLOCK_DEFINE` | event group, software timer, spinlock | `U_EVENT_*`, `U_TIMER_*`, `U_CRITICAL_*` |
| `U_TASK_DEFINE(name, stack)` + `U_TASK_START[_ON_CORE]` | task (stack + TCB static) | the stack size is in **bytes** (`StackType_t` is `uint8_t` on ESP-IDF); the task starts only at `U_TASK_START` |

How the macros work:
- `U_*_DEFINE` (except tasks) builds the object in an `__attribute__((constructor))` function, run before the scheduler starts, so init functions never create these objects.
- The handle is `NULL` until its own constructor has run, and constructors of different files run in **unspecified order**: a constructor never uses a handle defined in another file. Constructors are used for nothing else except the type tables (architecture.md §2).
- Start tasks in the component's init behind a guard, `if (h == NULL) U_TASK_START(...)`: starting a task twice re-initializes the running task's TCB.
- Stack sizes, priorities and depths come from Kconfig (`CONFIG_<COMPONENT>_*`), not literals (§2).

### Open `utils.h` fixes

| # | Issue | Fix |
|---|---|---|
| 1 | Handles have **external linkage** (`QueueHandle_t name = NULL;`) and `static` can't be put in front of the macro: two files using the same handle name fail at link time | Emit `static` handles by default; a `_SHARED` variant for the few handles that really are shared |
| 2 | `U_TASK_START` doesn't check whether the task already runs | Put the `if (name == NULL)` guard inside the macro |
| 3 | `U_TASK_DEFINE(name, stack_words)`: the value is bytes on ESP-IDF | Rename the parameter to `stack_bytes` |
| 4 | Static stream / message buffers hold `size − 1` bytes | Allocate and pass `size + 1` |
| 7 | `U_RINGBUFFER_DEFINE` doesn't check that the size is 4-aligned | `_Static_assert((buffer_size) % 4 == 0, ...)` |
| 8 | `U_QUEUE_SEND_ISR` ignores a full queue | Return the send result |

Places not yet following §1: `vm_exec.c` (`U_TASK_DEFINE(vm_exec_task_h, 6144)`) and `esp32s3_gpio_adc.c` (`U_TASK_DEFINE(adc_processing_task, 4096)`) hard-code their stack sizes: move them to Kconfig.

## 2. Constants and configuration

- **Internal constants** of one file (register addresses, bit masks, protocol offsets, internal limits) are `#define`d at the top of that `.c` file. They go in a header only when another component needs them.
- **Tunables** (buffer sizes, queue depths, stack sizes, task priorities / cores, timeouts, counts, IDs, anything a user may want to change) go in the owning component's `Kconfig` under its menu, used as `CONFIG_<COMPONENT>_<NAME>`. A limit the app should know is marked `#@limit` (published by `templates/sync.sh`).
- After adding or changing Kconfig options run `idf.py reconfigure`; after changing a default, `idf.py refresh-config --policy=kconfig` ([build.md](build.md) §3). Commit the resulting `sdkconfig` change.
- **No fallback `#ifndef X` / `#define X` pairs** for values that come from Kconfig or another header: a missing value must fail the build. Include guards are `#pragma once`.
- Kconfig bools in a runtime `if`: `IS_ENABLED(CONFIG_X)` (ESP-IDF's), because a disabled bool is not defined.

## 3. Changing code — no leftovers

- When replacing an API, name, macro or constant, **remove the old one** and update every caller in the same change. No compatibility aliases, `#define OLD NEW`, deprecated wrappers or duplicate definitions.
- Kconfig options are renamed or removed outright (and reconfigured), never shadowed by a second option.
- Dead code is removed, not commented out.
- Comments and docs describe the code as it is: no history ("was", "no longer", "ported from"), no dates of changes, no references to removed code.
- **No versioning before release.** Wire formats, stored records and descriptors change in place: no version bytes or fields, no "v2" labels, no code that accepts several formats. The firmware, the descriptors (`templates/sync.sh`) and the app are updated together.

## 4. Validation happens once, at the boundary

- Check pointers, ranges and state **once**, where data enters: router handlers (sizes are checked by `router_args`), public API functions, device ops, the VM loader. Use `ERR_CHECK` and the device range helpers.
- Internal helpers (`static` functions called only after validation) **do not re-check**. Their contract is "caller already checked"; say so in the comment if it isn't obvious.
- Validate each new value where it first appears (a pointer obtained by lookup is checked right there).

## 5. Naming

### 5.1 Rules

- **snake_case only**; macros and constants `UPPER_SNAKE`.
- **Type suffixes:**

  | Kind | Suffix | Example |
  |---|---|---|
  | function pointer / callback | `_f` | `cmd_f`, `err_sink_f` |
  | struct / union / plain typedef | `_t` | `sys_event_t`, `vm_obj_t` |
  | handle (opaque pointer to an instance) | `_h` | `err_h`, `vm_obj_h`, `vm_block_h` |
  | enum | `_e` | `err_sev_e`, `vm_exec_command_e` |

- **Fixed-width integers** (`uint8_t`, `int32_t`, …) and `bool`. No `int`, `unsigned`, `long`, `short` for data. Accepted: `(unsigned long)` casts for `%lu`, `_Generic` dispatch lists, API-mandated types (`esp_err_t`, `BaseType_t`, `size_t`).
- **Units in real notation** at the end of identifiers: `voltage_mV`, `current_mA`, `budget_mW`, `frequency_Hz`, `delay_us`, `period_ms`. Macros and Kconfig keep caps (`…_LIMIT_MV`); ESP-IDF fields keep IDF's spelling. Measured outputs are signed (`int32_t* out_mV`).
- **No leading underscore** on identifiers (reserved at file scope); private structs are `<module>_data_t`.
- **Hierarchical names for anything exposed** (public headers, Kconfig, error tags), from general to specific:

  | Thing | Pattern | Example |
  |---|---|---|
  | Core / system module API | `<module>[_<object>]_<action>` | `router_submit`, `sys_io_set_mode`, `sys_device_call` |
  | Router op numbers | `<MODULE>_OP_<NAME>` / `VM_OP_<NAME>` | `VM_OP_DATA`, `SYS_SYSTEM_OP_RESTART` |
  | VM API | `vm_<part>_<action>` | `vm_exec_stop`, `vm_loader_add_accessor` |
  | Device driver | `device_<chip>` component, `<CHIP>_OP_*` own ops, the create struct in its header | `TPS55289_OP_PROTECTION` |
  | Chip access (static, in the driver) | `chip_<action>(ctx, ...)` returning `err_h` | `chip_set_voltage` |
  | Board / application | `runit_<area>_<action>` | `runit_board_devices_init` |
  | Kconfig | `CONFIG_<MODULE>_<NAME>` | `CONFIG_SYS_EVENT_QUEUE_LEN` |
  | Error tag / module | `ERR_<MODULE>_<WHAT>` / `ERR_MOD_<MODULE>` | `ERR_IO_PIN_IN_USE`, `ERR_MOD_SYS_IO` |

  The module segment matches the component folder name.

- **A command is named for its value or action** (`DEADMAN`, `TIME`, `LEVEL`), never for a side: the read and the write of one value are the two sides of one op (CMD.MD "Read and write").

### 5.2 Current deviations (open)

| Area | Deviation | Should be |
|---|---|---|
| `vm_core` | `vm_internal_*` helpers declared in public headers (`vm_obj_access.h` macros use them) | a private header or a module prefix |
| Function-pointer typedefs | `vm_block_fn`, `vm_block_verify_fn`, `runit_boot_step_fn` | `vm_block_f`, `vm_block_verify_f`, `runit_boot_step_f` |

## 6. Module setup

- A file that logs defines `#define TAG "<module>"` (or `__FILE_NAME__`). Debug output is `ESP_LOGD`.
- File-private state lives in structs, not in loose `static` variables (no stray flags or counters): fields grouped and commented in one place, at no cost.
  - `static struct { ... } s_<module>;` holds everything that starts at zero. It has **no initializer**, so it stays in `.bss`: one non-zero field would move the whole struct, arrays included, to `.data` (a flash image copied to RAM at boot).
  - What must start non-zero (a `portMUX` spinlock, default options read before `init`) goes in a second, small `static struct { ... } s_<module>_preset = {...};`. A single such value stays a plain `static` (`s_policy_mux`).
  - Outside any struct, only what can't be a field: RTOS objects from the `U_*_DEFINE` macros, `__thread` variables, constant tables (`k_*`). Independent parts of one file may each have their own struct (`s_hooks`, `s_rx`).
- A component with router ops exports only its `cmd_f` (`<module>_router`); `runit` registers it (ROUTER.MD).

## 7. Comments and documentation

- Headers and macros are commented in Doxygen style (`@brief`, `@param`, `@return`), with a minimal usage example for new APIs.
- **Never edit or rewrite a comment block the user wrote**, even if it looks stale after a code change. Add your own comment, kept visually separate. Prefer one `/* ... */` block over many scattered `//` lines.
- **Every change updates the docs it makes stale, in the same change, without being asked**: the component's `*.MD` and `AGENTS.md`, this skill, `runit-root`, `templates/AGENTS.md`, `CLAUDE.md`, `.claude/memory.md` (the full list: `runit-root` SKILL.md §5). Docs describe the current state only and never reference removed code or legacy material unless the user asks.
- A component with no `*.MD`: ask the user for its purpose, then write the doc and an `AGENTS.md` in the style of the `sys_*` ones.

## 8. Working style

- Don't make large architectural assumptions. When requirements are ambiguous, present options and ask.
- Don't guess the purpose of unknown registers, hardware logic or schemas. Ask, then record the answer (HARDWARE.md, the component doc or this skill).
- Record user corrections and new rules in the matching skill file straight away.
