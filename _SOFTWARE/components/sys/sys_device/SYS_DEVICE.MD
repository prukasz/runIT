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
| `sys_device_errors.c` | The error policy: the walk over a reported chain and the response (below) |
| `sys_device_router.c/.h` | The remote interface (`ROUTER_KEY_DEVICE` = 0x05): parses `[id u16][op u16][args]` and calls `sys_device_call`. The only file here that includes the router; the app (`runit.c`) includes its header, nothing else does |
| `Kconfig` | `SYS_DEVICE_MAX` (32 instances), `SYS_DEVICE_TYPES` (16), `SYS_DEVICE_MAX_DEPS` (4 devices one device may use), `SYS_DEVICE_GUARDS` (2 contracts with a guard) |
| `SYS_DEVICE_NEW.md` | Adding a device: a driver on this registry |
| `AGENTS.md` | Rules for changing this folder |

## Commands

`[0x05][device id u16 LE][op u16 LE][args]`. A failure is wrapped in `ERR_DEV_ID{id}` and `ERR_DEV_OP{op}`, so the chain reads `KEY 05` → `ERR_DEV_ID` → `ERR_DEV_OP` → cause (`ERR_DEV_NOT_FOUND` comes without the op node: nothing ran). IDs are `0..0xFFFE`; `0xFFFF` (`SYS_DEVICE_ID_NONE`) is "no device" in pin references and in the sweeps below. There is no list op: an app probes IDs with INFO and reads what each answers with OPS.

```
boot:    SYS_DEVICE_TYPE_DEFINE in device_pca9685.c registers type 0x0041 (a constructor)
create:  [05][03 00][00 00][41 00][cfg]  -> size check, calloc(ctx_size), id = 3, create(ctx, cfg), add (3, 0x0041, ctx)
use:     [05][03 00][03 01][pin u8][duty u32] -> ID 3 -> its type's op table -> IO_HANDLER(SET_PWM_DUTY)(ctx, args)
remove:  [05][03 00][04 00]              -> uninstall(ctx), unregister, free
```

### Ops

An op is a u16, `[contract u8][op u8]`: the high byte is the contract, the low byte the op inside it (`SYS_DEVICE_OP(contract, n)`). Contract IDs are assigned in one place, `sys_device.h` (`SYS_DEVICE_CONTRACT_*`: 0x00 device, 0x01 IO, 0x02 PINS, 0x03 EVENT (`ON_EVENT`, SYS_EVENT.MD), 0x04 POWER_MONITOR, 0x05 POWER_VREG (SYS_POWER.MD), 0x06 POWER_MANAGER (events only, SYS_POWER_MANAGER.MD), 0x07 POWER_USB_PD, 0x08 POWER_SUPPLY (SYS_POWER.MD), 0x09 HBRIDGE (SYS_HBRIDGE.MD), 0xFF own). Device types are **u16** (CREATE, INFO, the type errors), assigned in `sys_device_types.h`; drivers start at 0x0040. A new contract takes the next free ID and numbers its ops from 0, without touching any other contract or any device's own ops.

| Op | Range | Meaning |
|---|---|---|
| `0x0000` CREATE `[type u16][create struct]` | the ID is free | `ERR_DEV_BAD_ID`, `ID_TAKEN`, `TABLE_FULL`, `TYPE_UNKNOWN`, `ERR_CMD_BAD_LEN` (struct size); the driver's failure wrapped in `ERR_DEV_INSTALL{id, type}` after `uninstall()` |
| `0x0001` RESET | lifecycle (driver) | outputs off, defaults |
| `0x0002` SUSPEND | lifecycle (driver), **required** | outputs safe; the device refuses writes (`ERR_DEV_SUSPENDED`) until RESUME, and counts as suspended even if a step failed |
| `0x0003` RESUME | lifecycle (driver), **required** | a failing resume leaves it suspended |
| `0x0004` REMOVE | generic | refused while another device uses it (`ERR_DEV_IN_USE{id, user}`); else `uninstall()`, unregistered, freed (also when `uninstall()` failed: its error is returned) |
| `0x0005` INFO → `{u16 type_id, u8 suspended}` | generic | from the type and `sys_device_base_t` |
| `0x0006` OPS → `u16[]` | generic | SUSPEND, RESUME, REMOVE, INFO, OPS, SET / GET_ERROR_POLICY, then the type's op table. The app reads what a device can do instead of trying ops |
| `0x0007` SET_ERROR_POLICY `{u8 importance, u32 actions[4]}` (17 B) | generic | the device's error policy (below). Importance > 4: `ERR_DEV_RANGE`. Allowed while suspended |
| `0x0008` GET_ERROR_POLICY → the same 5 bytes | generic | |
| `0x01xx..0xFExx` | contracts | one meaning on every device that has them (`0x01` IO: SYS_IO.MD) |
| `0xFFxx` | own | the device's own operations (`SYS_DEVICE_OWN_OP(n)`); two types may give the same number different meanings |

**Sweeps over the router**: SUSPEND, RESUME or RESET sent to device ID `0xFFFF` (`SYS_DEVICE_ID_NONE`, "no device") runs `sys_device_suspend_all` / `resume_all` / `reset_all`: `[05][FF FF][02 00]` suspends every device, `[05][FF FF][03 00]` resumes them, `[05][FF FF][01 00]` resets them (newest first, as suspend; no args, else `ERR_CMD_BAD_LEN`). Best effort as below; the first failure comes back as the device's own `ERR_DEV_ID` chain (not wrapped in an ID-NONE node, so the error policy still finds the device). A normal device frame, so an action can record or hold it (the app's onboard actions do, RUNIT.MD). REMOVE has no router sweep: `sys_device_remove_all` is the firmware's, run when the project changes.

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

- **Dependencies are declared, not inferred from IDs.** A driver's `create()` calls `sys_device_use(&c->base, dep)` for every device it will call (the device of each pin, ...), before using it. `dep` must exist (`ERR_DEV_NOT_FOUND{dep}`); up to `CONFIG_SYS_DEVICE_MAX_DEPS` distinct ones (`ERR_DEV_DEPS_FULL`). They are stored in the device's own `sys_device_base_t` (`dep_count`, `deps[]`). A CREATE from inside a `create()` is refused (`ERR_DEV_BAD_ARG`). The registry reads them: REMOVE of a used device answers `ERR_DEV_IN_USE{id, user}` (remove the user first), so no device is left calling a device that is gone, or a new one under the same ID.
- **Order = creation order.** A device can only use devices that exist when it is created and a used device can't be removed, so the instance list (kept in creation order) is a dependency order. IDs are free: a pin may be on a higher ID, as long as that device was created first.
- **The board's own devices.** After the onboard devices are created the app calls `sys_device_mark_system()` (boot step "system devices"): everything registered then is the board's, and `sys_device_remove_all` leaves it, so changing the project removes only the devices the project created. An explicit REMOVE of one still works, and the registry doesn't protect them from it; the suspend, resume and reset sweeps cover all.
- `sys_device_suspend_all`, `reset_all` and `remove_all` go newest first (users before what they use); `resume_all` oldest first. Best effort: every device is tried, the first error is returned and the others go to `err_report`. The app's safe state (`runit_enter_safe_state`, RUNIT.MD) calls `suspend_all`.

## Guards

A module can see every call of one contract's ops before the device does: `sys_device_set_guard(contract, fn)` (boot, one per contract, `CONFIG_SYS_DEVICE_GUARDS` = 2). `sys_device_call` (so remote commands too) asks the guard first; it either takes the call over (`*handled = true`: it runs the op itself with `sys_device_call_direct`, or refuses it) or passes it on. Its own failures get `ERR_DEV_OP` / `ERR_DEV_ID` like a device's. The guard runs **without the registry lock** and takes its own first, so it must not be reached from inside a device op (lock order: the guard's lock, then the registry's). User: the power manager guards POWER_VREG (SYS_POWER_MANAGER.MD).

## Concurrency

One recursive mutex covers the registry and every call into a device. It is needed: devices are called from the two router tasks, the event task (queued listeners, deferred ISR events), the ADC task (its threshold events, deferred), esp_timer callbacks and the VM; and ops change chip state in several I2C transfers that must not interleave. Device-to-device calls nest (recursive).

Per-device locks were considered and not taken: REMOVE during a call would need reference counts, and an inline event published inside an op may reach a higher ID (lock order). With short ops the single lock costs one I2C sequence of waiting.

Rules (they keep the lock short and deadlock-free):
- **A background task that an op may wait for publishes with `SYS_EVENT_DEFER` in the event's flags** (everything deferred to the event task), never inline: an inline listener run in that task could need the lock the waiting op holds. GPIO ESP's ADC task does this (SET_MODE / RESET wait for it to rebuild the ADC channel set).
- **An op must not sleep or wait for anything but its own I/O.** A long sequence runs as steps: a timer calls `sys_device_call(id, OWN_OP)` for each step (TPS55289 rise). The bounded waits are a chip's start-up after a power pin (TPS55289 EN, 10 ms) and the power mux's break-before-make pause (1..100 ms, configured).
- **A driver must not hold a lock of its own while it calls `sys_device_call` or `sys_event_publish`** (a listener may call back into it: GPIO ESP collects ADC events under `gpio_mutex` and publishes them after).

## Adding a device

How to write a driver (type ID, struct, ops, create / uninstall, hooks, registration) is in `SYS_DEVICE_NEW.md`.

## Error policy

Error severities never change: a device's **importance** only decides which of its errors get a **response**. The policy is in `sys_device_errors.c`; the app wires its runner and VM stop in `runit_error_policy.c`.

Each instance has `{importance, actions[4]}` (`sys_device_error_policy_t`, the action IDs are u32), set with SET_ERROR_POLICY, read with GET_ERROR_POLICY. A new instance starts at LOW with no actions; REMOVE forgets it (not stored, like the instances).

| Importance | Admits (gets a response) |
|---|---|
| 0 NONE | nothing: the device's errors **and every deeper cause** are ignored, critical included (a device under test). The chain is still logged and sent |
| 1 LOW (start) | CRITICAL |
| 2 MEDIUM | HIGH, CRITICAL |
| 3 HIGH | MEDIUM, HIGH, CRITICAL |
| 4 CRITICAL | LOW and up |

A CRITICAL error first stops the VM (`sys_device_set_vm_stop`, the app registers `vm_stop_on_fault`; stage `VM_STOP` of `ERR_DEV_FAULT_RESPONSE`), so the program stops writing, then suspends every device.

Rule: CRITICAL at any importance but NONE, else `severity + importance > 4`. `actions[i]` is the record ID (sys_actions, u32; 0 = none) for severity LOW, MEDIUM, HIGH, CRITICAL.

**Walk** (`sys_device_handle_error`, called by the app's error sink for every reported chain, before the record is sent and whatever `send_errors` says): from the outermost node; a device is an `ERR_DEV_ID{id}` node (the registry wraps every failed call in one, so every device error has one). Each device once, at its first mention, with `err_chain_sev` **from its node down** (`ERR_DEV_ID` / `ERR_DEV_OP` are NONE and add nothing; an explicit severity below still is the verdict). A device at NONE ends the walk. A device that no longer exists (a failed CREATE) gets a response only for a critical error, without an action. A critical node above every device goes to the system response.

**Response** (here too): CRITICAL → the VM stopped (`vm_stop_on_fault`), then every device suspended (`sys_device_suspend_all`), then the device's action for that severity through the **action runner** the app sets (`sys_device_set_action_runner`, like `sys_event_set_action_runner`, so `sys_device` doesn't know `sys_actions`). runit's runner queues it on the router (`sys_actions_request`, normal priority) and refuses an action whose own failure (`ERR_ACTION_STEP{id}`) is in the chain. Without a runner actions don't run; CRITICAL still suspends. A failed response is wrapped in `ERR_DEV_FAULT_RESPONSE{id, sev, stage, action}` and returned to the sink, which logs it and sends it as a record, never reports it (no policy on it).

**Locks**: the walk reads a policy under a short critical section (`s_table_mux`), not the registry lock, so a reporting task never waits for an op in progress. The response itself (`suspend_all`) takes the registry lock.

## Errors (`ERR_MOD_SYS_DEVICE` = 6)

Registry: `ERR_DEV_BAD_ARG`, `BAD_ID`, `NOT_FOUND`, `ID_TAKEN`, `TABLE_FULL`, `TYPE_UNKNOWN`, the context nodes `ID {id}` and `OP {op}` (severity NONE), `UNKNOWN_OP {op}`. Devices: `SUSPENDED`, `NO_MEM` (critical), the context node `INSTALL {id, type}`, `IN_USE {id, user}`, `DEPS_FULL {id}`, `BAD_PIN`, `PIN_LOCKED {id, pin}` (a write to a pin locked by its owner), `RANGE {id, i32 value, i32 min, i32 max}` (signed: thresholds may be negative), `FAULT_RESPONSE {id, sev, stage, action}` (the policy's response failed; diagnostics only). A misdefined or duplicate type stops the boot with an assert, like a duplicate error module.
