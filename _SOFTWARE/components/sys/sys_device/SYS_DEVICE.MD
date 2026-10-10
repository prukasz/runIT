# sys_device — device registry

The registry has two lists: **the device types** (one `sys_device_type_t` per driver, registered by the driver itself) and **the instances** (device ID, type, struct). It does everything that is the same on every device:
- CREATE: checks the create struct's size, allocates the device's struct, calls the driver's `create()`, rolls back with `uninstall()` on a failure, adds the instance;
- REMOVE: `uninstall()`, unregisters, frees;
- INFO and OPS;
- runs every other op from the type's op table; an op it doesn't have, such as a contract the device doesn't implement, answers `ERR_DEV_UNKNOWN_OP{op}`.

The driver keeps its state (suspended included), builds and releases its hardware, makes its outputs safe on SUSPEND and refuses what it can't do.

| File | Contents |
|---|---|
| `sys_device.h` | Op convention, lifecycle table, `sys_device_base_t`, `sys_device_type_t`, `SYS_DEVICE_TYPE_DEFINE`, `ERR_SYS_DEVICE_MAP`, calls and sweeps |
| `sys_device_types.h` | **The type IDs**, one X-macro list (`SYS_DEVICE_TYPE_LIST` → `SYS_DEVICE_TYPE_<NAME>`); a duplicate fails to compile |
| `sys_device.c` | Types, instances (creation order), generic ops, calls, sweeps. Depends on `cmd` only |
| `sys_device_router.c/.h` | The remote interface (`ROUTER_KEY_DEVICE` = 0x05): parses `[id u16][op u16][args]` and calls `sys_device_call`. The only file here that includes the router; `main` includes its header, nothing else does |
| `Kconfig` | `SYS_DEVICE_MAX` (32 instances), `SYS_DEVICE_TYPES` (16), `SYS_DEVICE_MAX_DEPS` (4 devices one device may use) |

## Commands

`[0x05][device id u16 LE][op u16 LE][args]`. A failure is wrapped in `ERR_DEV_ID{id}` and `ERR_DEV_OP{op}`, so the chain reads `KEY 05` → `ERR_DEV_ID` → `ERR_DEV_OP` → cause (`ERR_DEV_NOT_FOUND` comes without the op node: nothing ran). IDs are `0..0xFFFE`; `0xFFFF` (`SYS_DEVICE_ID_NONE`) is "no device" in pin references.

```
boot:    SYS_DEVICE_TYPE_DEFINE in device_pca9685.c registers type 0x0041 (a constructor)
create:  [05][03 00][00 00][41 00][cfg]  -> size check, calloc(ctx_size), id = 3, create(ctx, cfg), add (3, 0x0041, ctx)
use:     [05][03 00][03 01][01][duty]    -> ID 3 -> its type's op table -> IO_HANDLER(SET_PWM_DUTY)(ctx, args)
remove:  [05][03 00][04 00]              -> uninstall(ctx), unregister, free
```

### Ops

An op is a u16, `[contract u8][op u8]`: the high byte is the contract, the low byte the op inside it (`SYS_DEVICE_OP(contract, n)`). Contract IDs are assigned in one place, `sys_device.h` (`SYS_DEVICE_CONTRACT_*`: 0x00 device, 0x01 IO, 0x02 PINS, 0x03 EVENT (`ON_EVENT`, SYS_EVENT.MD), 0x04 POWER_MONITOR, 0x05 POWER_VREG (SYS_POWER.MD), 0xFF own). Device types are **u16** (CREATE, INFO, the type errors), assigned in `sys_device_types.h`; drivers start at 0x0040. A new contract takes the next free ID and numbers its ops from 0, without touching any other contract or any device's own ops.

| Op | Range | Meaning |
|---|---|---|
| `0x0000` CREATE `[type u16][create struct]` | the ID is free | `ERR_DEV_BAD_ID`, `ID_TAKEN`, `TABLE_FULL`, `TYPE_UNKNOWN`, `ERR_CMD_BAD_LEN` (struct size); the driver's failure wrapped in `ERR_DEV_INSTALL{id, type}` after `uninstall()` |
| `0x0001` RESET | lifecycle (driver) | outputs off, defaults |
| `0x0002` SUSPEND | lifecycle (driver), **required** | outputs safe; the device refuses writes (`ERR_DEV_SUSPENDED`) until RESUME, and counts as suspended even if a step failed |
| `0x0003` RESUME | lifecycle (driver), **required** | a failing resume leaves it suspended |
| `0x0004` REMOVE | generic | refused while another device uses it (`ERR_DEV_IN_USE{id, user}`); else `uninstall()`, unregistered, freed (also when `uninstall()` failed: its error is returned) |
| `0x0005` INFO → `{u16 type_id, u8 suspended}` | generic | from the type and `sys_device_base_t` |
| `0x0006` OPS → `u16[]` | generic | REMOVE, INFO, OPS, then the type's op table. The app reads what a device can do instead of trying ops |
| `0x01xx..0xFExx` | contracts | one meaning on every device that has them (`0x01` IO: SYS_IO.MD) |
| `0xFFxx` | own | the device's own operations (`SYS_DEVICE_OWN_OP(n)`); two types may give the same number different meanings |

A device's class is the set of ops it answers (OPS returns it). The op table (`sys_device_op_t {u16 op, cmd_f fn}`) lists only the ops the driver has, so its size doesn't depend on how many contracts exist.

### Contract tables (X-macros)

A contract is written once, as a table `X(NAME, op number, args struct)` (`SYS_DEVICE_LIFECYCLE` here, `SYS_IO_CONTRACT` in `sys_io.h`, `SYS_PINS_CONTRACT` in `sys_pins.h`, the power contracts in `sys_power_*.h`). Four things come out of it, all keyed by `NAME`:

| Generated | Example |
|---|---|
| op ID | `IO_OP_SET_LEVEL` = `0x0100` |
| args typedef | `io_args_SET_LEVEL_t` = `io_level_args_t` |
| handler macro | `IO_HANDLER(SET_LEVEL) { … a->level … }` (parses exactly `sizeof(args)` bytes, else `ERR_CMD_BAD_LEN`; body sees `ctx`, `a`, `out…`) |
| table row | `IO_ENTRY(SET_LEVEL)` |

The handler's struct and the row's op ID both come from `NAME`, so an op can't be registered with another op's struct, and a device doesn't spell any struct. The registry strips the op and hands the handler the args only. A new contract = a table, the three small generator macros (see `sys_pins.h`) and a `SYS_DEVICE_CONTRACT_*` ID. Numbers inside a table are never reused.

Internal callers use `sys_device_call(id, op, args, len, out, cap, &out_len)`: the same call without a transport, the args used in place (no copy, no size limit). The contract wrappers (`sys_io_set_level(pin, level)`, ...) are built on it, and so are event listeners with a device target, device timers (TPS55289's rise steps) and the board creating its devices (op CREATE, args `[type][cfg]`).

## Dependencies, safe state and order

- **Dependencies are declared, not inferred from IDs.** A driver's `create()` calls `sys_device_use(id, dep)` for every device it will call (the device of each pin, ...), before using it. `dep` must exist (`ERR_DEV_NOT_FOUND{dep}`); up to `CONFIG_SYS_DEVICE_MAX_DEPS` distinct ones (`ERR_DEV_DEPS_FULL`). The registry keeps them with the instance: REMOVE of a used device answers `ERR_DEV_IN_USE{id, user}` (remove the user first), so no device is left calling a device that is gone, or a new one under the same ID.
- **Order = creation order.** A device can only use devices that exist when it is created and a used device can't be removed, so the instance list (kept in creation order) is a dependency order. IDs are free: a pin may be on a higher ID, as long as that device was created first.
- `sys_device_suspend_all` / `remove_all` go newest first (users before what they use); `resume_all` oldest first. Best effort: every device is tried, the first error is returned and the others go to `err_report`. The app's safe-state handler (`main.c`) calls `suspend_all`.
- Replaces (2026-10-10) the rule "a device may only use lower IDs" (`sys_device_check_dep`, `ERR_DEV_PIN_ORDER`, retired): it forced the creation order onto the IDs and didn't stop a REMOVE of a used device.

## Concurrency

One recursive mutex covers the registry and every call into a device. It is needed: devices are called from the two router tasks, the event task (queued listeners, deferred ISR events), the ADC task (inline listeners of its threshold events), esp_timer callbacks and, later, the VM; and ops change chip state in several I2C transfers that must not interleave. Device-to-device calls nest (recursive).

Per-device locks were considered and not taken: REMOVE during a call would need reference counts, and an inline event published inside an op may reach a higher ID (lock order). With short ops the single lock costs one I2C sequence of waiting.

Rules (they keep the lock short and deadlock-free):
- **A background task that an op may wait for publishes with `sys_event_post`** (always deferred to the event task), never `sys_event_publish`: an inline listener run in that task could need the lock the waiting op holds. GPIO ESP's ADC task does this (SET_MODE / RESET wait for it to rebuild the ADC channel set).
- **An op must not sleep or wait for anything but its own I/O.** A long sequence runs as steps: a timer calls `sys_device_call(id, OWN_OP)` for each step (TPS55289 rise). The one bounded wait is a chip's start-up after a power pin (TPS55289 EN, 10 ms).
- **A driver must not hold a lock of its own while it calls `sys_device_call` or `sys_event_publish`** (a listener may call back into it: GPIO ESP collects ADC events under `gpio_mutex` and publishes them after).

## Writing a device

One component, `components/devices/device_<chip>/`. Reference: `device_pca9685`.

1. **Type ID:** a line in `sys_device_types.h` (`X(<CHIP>, 0x00..)`), next free number.
2. **Header:** the create struct (packed, the wire form) and the chip's limits. Nothing else is exported.
3. **Struct:** `sys_device_base_t base` first (`id`, `suspended`), then what the device needs. The registry allocates it zeroed and fills `id`.
4. **Ops** with the contract handler macros (`DEVICE_HANDLER(SUSPEND)`, `IO_HANDLER(SET_LEVEL)`, `PINS_HANDLER(LOCK)`, rows `DEVICE_ENTRY` / `IO_ENTRY` / `PINS_ENTRY`); `ctx` is the struct. Own ops (`SYS_DEVICE_OWN_OP(n)`) use `SYS_DEVICE_HANDLER(fn, args_t)` and a `{op, fn}` row. Writes start with `ERR_TRY(sys_device_check_active(&c->base))`. Range-check values the chip would reject (`ERR_DEV_RANGE`) and pins (`ERR_DEV_BAD_PIN`); don't clamp. `k_ops` must answer SUSPEND and RESUME and must not list REMOVE / INFO / OPS (the registry asserts both at boot).
5. **`create(void* ctx, const void* cfg)`:** checks the cfg (ranges), declares every device it uses (`sys_device_use` for each pin's device), fills the struct, builds the hardware, recording each step it built. No allocation, no registration: the registry does both.
6. **`uninstall(void* ctx)`:** releases what was built. Every step runs, guarded by what was built (`SYS_DEVICE_KEEP_FIRST`). Used by REMOVE and by the create rollback; the registry frees the struct.
7. **SUSPEND** must leave every output safe; it is the fault path.
8. **Register:** `SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_<CHIP>, .cfg_size = sizeof(cfg_t), .ctx_size = sizeof(ctx_t), .create = create, .uninstall = uninstall, SYS_DEVICE_OPS(k_ops))` at the end of the file, and `WHOLE_ARCHIVE` in the component's `CMakeLists.txt` (nothing references the driver; without it the linker drops the registration). Nothing to add in `main`: every component under `components/devices` is built and linked.

## Errors (`ERR_MOD_SYS_DEVICE` = 6)

Registry: `ERR_DEV_BAD_ARG`, `BAD_ID`, `NOT_FOUND`, `ID_TAKEN`, `TABLE_FULL`, `TYPE_UNKNOWN`, the context nodes `ID {id}` and `OP {op}` (severity NONE), `UNKNOWN_OP {op}`. Devices: `SUSPENDED`, `NO_MEM` (critical), the context node `INSTALL {id, type}`, `IN_USE {id, user}`, `DEPS_FULL {id}`, `BAD_PIN`, `PIN_LOCKED {id, pin}` (a write to a pin locked by its owner), `RANGE {id, i32 value, i32 min, i32 max}` (signed since 2026-10-10: thresholds may be negative). Tag 14 (`PIN_ORDER`) is retired (dependencies are declared). Tags 7–9 (`TYPE_TAKEN`, `TYPE_FULL`, `WRONG_TYPE`) are retired: a misdefined or duplicate type stops the boot with an assert, like a duplicate error module.

## Not yet

- **The board config** that creates the onboard devices, and protecting them from REMOVE.
- **A device list for the app:** it can probe IDs with INFO, and read what each answers with OPS.
- **Device descriptions for the app** (`//#device` annotations and the generator) for the new create structs.
- **Per-device error policy:** importance, actions.
