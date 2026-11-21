# sys_device — the device registry

Folder `components/sys/sys_device/`. A device is an instance of a type (a driver) under an ID, reached only by its ops: nothing includes a driver, and a driver registers itself. The registry keeps the devices, runs their ops one at a time per device, knows which device uses which, and gives back what a device took when it goes.

## Components

| Component | Role | Who requires it |
|---|---|---|
| `sys_device/` | The registry: slots, types, calls, the sweep, remote devices | Everything that calls a device or is one |
| [`sys_device_event/`](../sys_device_event/SYS_DEVICE_EVENT.MD) | The EVENT contract (`ON_EVENT`) and the subscription of a device by its ID | Drivers that listen to events |
| [`sys_device_timer/`](../sys_device_timer/SYS_DEVICE_TIMER.MD) | A device's timer: its expiry is a step of its owner (`sys_device_step`) | Drivers that run something later or every so often |
| [`sys_device_router/`](../sys_device_router/SYS_DEVICE_ROUTER.MD) | The registry as commands (root key `MODULE_ID_SYS_DEVICE`) | Whoever builds the frame; it registers itself |
| [`sys_device_policy/`](../sys_device_policy/SYS_DEVICE_POLICY.MD) | The device error policy (root key `MODULE_ID_POLICY`): follows the registry's events, responds in a task of its own | The app's error sink |

Inside `sys_device/`:

| File | Role |
|---|---|
| `sys_device.h` | For every caller: IDs, ops and contracts, the calls, `sys_device_create_system` (CREATE of the board's own device: it builds the frame), the sweep |
| `sys_device_driver.h` | For a driver: the type's ID and definition, the op table and handler macros, own ops, resources, uses, the caller, the generation, a step and an event delivery |
| `sys_device_hooks.h` | For the app's wiring: the routed call, interceptors, the remote hook |
| `slots/` | Private: the table of devices (a lock, flags, resources and use masks per ID) |
| `registry/` | Private: types, CREATE, the registry's ops, resources, uses |
| `call/` | Private: a call (interceptor, device, caller), the task's nesting, the sweep |
| `remote/` | Private: devices of another board |
| `errors/`, `events/` | The error tags and messages, the events the registry publishes |
| `Kconfig` | Types, interceptors, the lock timeout, the room for held events |

## IDs, ops, contracts

- A device ID is 0..`SYS_DEVICE_COUNT`-1 (64); `SYS_DEVICE_ID_NONE` (`0xFF`) is never a device.
- An op is `[contract u8][op u8]`, `SYS_DEVICE_OP(contract, n)`, with a read side, a write side or both. The read takes the key and answers what the write takes.
- A contract is one table `P_OPERATIONS(X, P)` of rows `X(P, OP_NAME, n, key_t, value_t, flags)` (`cmd_table.h`). `SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_P, MODULE_ID_P)` in the contract's header, after it includes the contract's `<contract>_id.h` (its `MODULE_ID_DECLARE`), gives `CONTRACT_P_<OP_NAME>`, `CONTRACT_P_FLAGS_<OP_NAME>`, `CONTRACT_P_key_<OP_NAME>_t`, `CONTRACT_P_value_<OP_NAME>_t`.

**Contract IDs** are the numbers of their modules (`module_id.h`): there is no list of them and no file includes them all. The same number is the root key of the module's commands, the module of its errors and of its events.

| ID | Owner |
|---|---|
| `0x2F` | The registry (`MODULE_ID_SYS_DEVICE`): the device's lifecycle ops |
| `0x01`..`0x1F` | A contract: `MODULE_ID_DECLARE(MODULE_ID_P, 0x..)` in its `<contract>_id.h` and `SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_P, MODULE_ID_P)` in its header |
| `0xFF` | A device's own ops, a table in its driver (`SYS_DEVICE_OWN_DECLARE`); no contract takes it |

The ID is written once, as two hex digits, in the `MODULE_ID_DECLARE` of its `<contract>_id.h`; everything else names it `MODULE_ID_P`. Two contracts with one ID fail to compile where both headers meet, and fail `templates/sync.sh` (`module_id.h`). A contract may be its header only. The ID is wire format and the module of the contract's events.

**Op flags** (the last column of a contract's table, so no driver can leave them out):

| Flag | Meaning |
|---|---|
| `SYS_DEVICE_OP_ALWAYS` | The write runs while the device is suspended (an event delivery, a pin lock, a bus transfer) |
| `SYS_DEVICE_OP_LOCAL` | C only (its args carry a pointer, or it is a driver's op): refused as a command and on a remote device, left out of OPS |
| `SYS_DEVICE_OP_NOLOCK` | The handler runs without the lock of the slot, counted in flight; REMOVE waits for it. The driver guards it and bounds its time |
| `SYS_DEVICE_OP_DIRECT` | A read that takes something or costs (UART RECEIVE, I2C PROBE / SCAN / RAW): a WIRELESS remote device sends it and waits for the answer instead of serving it from a watch's copy, which would run it every period (SYS_DEVICE_REMOTE.MD "A call") |
| `SYS_DEVICE_OP_TAIL` | The write's payload is its value (the head, packed) and then bytes of any number; its handler is `OP_DECLARE_WRITE_TAIL` |

**The registry's ops** (contract `MODULE_ID_SYS_DEVICE`):

| Op | n | Side | Notes |
|---|---|---|---|
| CREATE | `0x00` | W | `[type u16][flags u8][create struct]`; flag `SYS_DEVICE_CREATE_SYSTEM`: the board's own device |
| RESET | `0x01` | W | The type's own op, if it has one |
| SUSPEND | `0x02` | W | The type's `suspend()`, then the flag (set even when the hook failed) |
| RESUME | `0x03` | W | Only while suspended; a failing `resume()` leaves it suspended |
| REMOVE | `0x04` | W | See "Lifecycle" |
| INFO | `0x05` | R | `sys_device_info_t`: type ID, suspended |
| OPS | `0x06` | R | A list of `[op u16][sides u8]`, LOCAL ops left out |
| CREATE_REMOTE | `0x08` | W | `[type u16][sys_device_remote_t]` |

`0x07` is unused: never assign it.

## Calls

```c
ERR_TRY(SYS_DEVICE_SET(motor, CONTRACT_HBRIDGE, OP_DRIVE, .channel = 0, .drive = 800));
int32_t mA; ERR_TRY(SYS_DEVICE_GET(mon, CONTRACT_PMON, OP_CURRENT, &mA, .channel = 1));
```

`SYS_DEVICE_SET / GET` build the frame from the fields given, `SYS_DEVICE_WRITE / READ` take a frame the caller has; all end in `sys_device_call(id, dir, op, in, out)`. The op's number and the sizes come from the contract's table, so they can't be paired wrongly.

One call, in order:

1. The contract's interceptor, if any, without a lock: it may refuse the call or change the payload for the device. After step 5 it is told how the call ended.
2. The lock of the slot, waited for at most `CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS` (500).
3. The checks: the device exists, has the op and the side, is not suspended for a write (ALWAYS aside), the op is not LOCAL for a command or a remote device.
4. The handler, with the driver's struct and the payload. A NOLOCK op and a remote device's op run after the lock is let go.
5. The lock let go; what the op changed is published (see "Events").

Every error of a call is wrapped in `ERR_DEV_OP{op, dir}` and `ERR_DEV_ID{id}`.

### A step

`sys_device_step(id, gen, op)` is the call of a device that runs by itself (a timer's expiry, `sys_device_timer`): the write of its own op, without args, to device `id` of generation `gen` (`sys_device_gen(id)`, taken in its `create()`). It is the same call with two differences in step 2 and 3:

- The lock is taken only when it is free now; else the step is skipped. The timers' task never waits for its owner's lock, so a busy or hung owner does not hold up the other timers on its own account (nested calls to dependencies inside a step still wait up to `CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS`).
- It is skipped too when the device is gone or is another one (its generation), is suspended, or is refused by the safe-state sweep.

A skipped step is no error: the next comes in its own time. An error of the op itself is returned (the timer reports it), so the handler of a step stops its timer on a failure. The op is LOCAL and needs no ALWAYS: a step never runs on a suspended device, so its driver keeps no flag of its own for that.

### Delivering an event

`sys_device_deliver(id, gen, op, args, args_len)` is the call used by subscriptions (`sys_device_event_subscribe`, `sys_device_event_subscribe_op`) to write `op` with `args` to device `id` of generation `gen`:

- Unlike a step, delivery waits for the device's lock (`sys_device_slot_lock`) and runs on a suspended device when `op` has `SYS_DEVICE_OP_ALWAYS`.
- Like a step, when the device is gone or another device has taken the ID (generation mismatch), delivery returns `NULL` instead of failing or reaching the wrong device.

### Concurrency

- **One lock per device**, not recursive. A NOLOCK op is the only way past it.
- **Nesting**: a driver's op that calls the device it uses holds both locks, its own first. The declared uses give that order, and a use can't close into a cycle, so two tasks never wait for each other.
- **`ERR_DEV_REENTRY{id}`**: the task is inside an op of this device already.
- **`ERR_DEV_BUSY{id, op}`**: the lock was not free in time; `op` is the op that holds it. One device that hangs fails its callers, it doesn't hold up their tasks for good.
- **The caller and self**: `sys_device_caller()` is the device whose op made the call (in `create()`, the device being created), `SYS_DEVICE_ID_NONE` for a command, a module or a listener. `sys_device_self()` is the device whose op runs now (`SYS_DEVICE_ID_NONE` outside an op), and `sys_device_self_suspended()` tells whether that device is suspended (for a `READ` or `ALWAYS` op such as `ON_EVENT`).

### Owners

A device that gives something out to one device (a pin's lock, a bus address) keeps who took it: `sys_device_caller()` of the op that took it.

- The owner is in the error of whoever asks next: `ERR_DEV_PIN_LOCKED{id, pin, owner}`, `ERR_I2C_ADDR_TAKEN{bus, addr, owner}`.
- Only the owner uses and frees it. A pin: the IO writes (LEVEL, PWM_DUTY, INTR, ...) pass for the owner and are refused to everyone else with `ERR_DEV_PIN_LOCKED{owner}`, so the owner needs no op of its own; UNLOCK from anyone else is `ERR_DEV_PIN_NOT_OWNED{owner}`. A bus address: XFER and DETACH from anyone else are `ERR_I2C_NOT_OWNER{owner}`.
- LOCK, UNLOCK, ATTACH, DETACH and XFER are LOCAL: no command reaches them. The owner `SYS_DEVICE_ID_NONE` is no device (a module's C, the chip's own peripheral, a bus's RAW for the one transfer): a pin it holds refuses every write.
- So a driver reaches what it took only from inside an op of its device (`create()`, `suspend()`, `resume()`, a timer's op and ON_EVENT included), never from a task of its own that calls the bus or the pin directly.

### Events of an op

An event published inside an op is not delivered there. The task's outermost `sys_device_call` holds the events of everything under it (`sys_event_hold`, `CONFIG_SYS_DEVICE_EVENT_HOLD_BYTES` = 256 B on that frame) and releases them when its last lock is gone. An inline listener then runs in the same task and may take the lock of any device, the publisher's and the caller's included.

- "Inline" for an op means "before the outermost call returns", not "before the op ends". An op that needs a reaction before its own end calls that device.
- An event that does not fit the hold goes to its lane's task.
- A NOLOCK op that blocks for long publishes with `SYS_EVENT_PUBLISH_DEFER`: its events would wait for its return.

## Lifecycle

**CREATE**: the lock of the slot is taken, the slot takes a new generation (so `create()` hands out its own, `sys_device_gen`), the driver's struct is allocated zeroed, `create()` runs. When it worked the slot is installed (`installed` last). When it failed the resources added so far are released, the uses declared are taken back, the struct is freed, and the error is wrapped in `ERR_DEV_INSTALL{id, type}`.

**REMOVE**, with the lock held:

1. Refused for a board device (`ERR_DEV_SYSTEM`).
2. Refused while other devices use it (`ERR_DEV_IN_USE{users}`). The check marks the device as being removed in the same critical section a use is declared in, so from here no device can start using it.
3. The NOLOCK calls in flight are waited for.
4. `suspend()`, unless the device is suspended already: the safe state is its last state.
5. The resources are released, the last added first.
6. The slot is freed and the struct too, even if a step failed.

A remote device runs none of its type's hooks here: SUSPEND and RESUME change only the flag, REMOVE frees only where it is.

### Resources

What a device takes belongs to it, and the registry gives it back: a driver has no teardown.

```c
ERR_TRY(sys_device_res_add(id, &c->bus_res, bus_release, c));  // before the resource is taken
ERR_TRY(ERR_ESP(i2c_new_master_bus(&cfg, &c->bus)));           // bus_release deletes c->bus unless it is NULL
```

- A resource is a node (`sys_device_res_t`) in the driver's struct with a release function and its argument. No allocation.
- It is added **before** it is taken, and its release is guarded by the resource's own state, so it gives back only what was taken.
- Released when the device is removed and when its `create()` fails, the last added first, with the lock of the owner held and the struct still there. Every release runs; the first error is returned.
- The take functions add theirs: a pin (`sys_io_claim_*`, with its subscription), a bus address (`sys_i2c_dev_attach`), a timer (`sys_device_timer_init`). A driver adds only what no one else knows (the ESP32-S3's I2C controller).
- A failed `create()` does not run `suspend()`: the struct is half built, and `suspend()` acts on other devices. A step that must be undone on the hardware is a resource added before that step.

### Uses

`sys_device_use(user, dep)` (or `sys_device_require_op`, which also checks that `dep` has the op) declares that a device calls another. The take functions do it. It gives:

- REMOVE of `dep` refused while `user` exists.
- The order of a sweep and of the locks.
- `ERR_DEV_CYCLE{user, dep}` when `dep` uses `user` already, itself or through others.
- A `dep` that is a remote device is allowed: `ERR_DEV_REMOTE_DEP{id, dep}` (LOW) tells it once, because `user`'s calls to `dep` go over the way (latency, a WIRED wait under the lock of `user`).

Two `uint64_t` masks per slot (`uses`, `used_by`), cleared when the device goes.

## Sweep

`sys_device_sweep(op)` runs SUSPEND, RESUME, RESET or REMOVE on every device. A command does the same: the op written to `SYS_DEVICE_ID_NONE` (`sys_device_router`).

- **Order**: a device before the devices it uses; RESUME the other way round.
- **Skipped**: REMOVE leaves the board's own devices; RESET a device whose type has none.
- **Best effort**: all are tried; the first error is returned, the others reported. A device whose lock is not free in time fails with `ERR_DEV_BUSY` and the sweep goes on.
- **Events**: one for the sweep (SUSPENDED / RESUMED / RESET with source `SYS_DEVICE_ID_NONE`), after its last device. A REMOVE sweep publishes each device's REMOVED.

**SUSPEND of every device is the fault safe state.** While that sweep runs, a write that comes from no device (a command, the VM, a module) and a CREATE are refused on every device with `ERR_DEV_SUSPENDED`, also on the ones the sweep has not reached. A device's own calls pass, so a `suspend()` hook reaches the devices it uses, which are suspended after it. A step (a timer's) comes from no device: it is skipped while the sweep runs.

## Events

Module `MODULE_ID_SYS_DEVICE` (the registry's number), the table `SYS_DEVICE_EVENTS` in `events/sys_device_events.h`. Source = the device (or `SYS_DEVICE_ID_NONE` for a sweep), channel 0.

| Event | Flags | Payload | When |
|---|---|---|---|
| `SYS_DEVICE_EV_SUSPENDED` | NOTIFY | | The device went into the safe state |
| `SYS_DEVICE_EV_RESUMED` | NOTIFY | | It left it |
| `SYS_DEVICE_EV_RESET` | NOTIFY | | A RESET of its type worked |
| `SYS_DEVICE_EV_CREATED` | | `gen u8` | The device exists |
| `SYS_DEVICE_EV_REMOVED` | | `gen u8` | The device is gone |

`gen` is the slot's generation: it counts the CREATEs of the ID (1..255, then 1 again), one that failed included. Two tasks publish after they let the lock go, so a REMOVED may reach a listener after the CREATED of the next device with that ID. A listener that keeps state per device keeps the generation of its CREATED and takes a REMOVED only with that generation (`sys_device_policy` does).

## A driver

One source, no header, nothing includes it. A driver of a SoC peripheral with parts of their own (the ESP32-S3 GPIO: PWM on LEDC, an ADC task) splits them into sources of its folder with a header private to it, still without `INCLUDE_DIRS`; the type is declared and defined in the driver's source:

```c
SYS_DEVICE_TYPE_DECLARE(PCA9685, 0x....)                             // the type ID, once: PCA9685_TYPE

typedef struct { uint8_t id; ... } pca_t;                            // the driver's struct
#define SYS_DEVICE_CTX pca_t                                         // every body below sees it as `c`

OP_DECLARE_WRITE(CONTRACT_IO, OP_LEVEL) { return chip_set(c, args->pin, args->level); }  // c, args, out
OP_DECLARE_READ(CONTRACT_IO, OP_LEVEL)  { return CMD_REPLY(CONTRACT_IO, OP_LEVEL, .pin = args->pin, .level = ...); }

static err_h create(uint8_t id, pca_t* c, const pca9685_cfg_t* cfg) { ... }  // checks cfg, takes resources
static err_h suspend(pca_t* c) { ... }                               // the safe state; also the last state
static err_h resume(pca_t* c) { ... }

static const sys_device_op_t k_ops[] = {OP_ENTRY(CONTRACT_IO, OP_LEVEL), ...};
SYS_DEVICE_TYPE_DEFINE(PCA9685, pca9685_cfg_t, create, suspend, resume, k_ops)
```

- **Type ID**: the driver's own, four hex digits, at least `SYS_DEVICE_TYPE_FIRST`; wire format. `SYS_DEVICE_TYPE_DECLARE(NAME, 0x....)` at the top of the source writes it once: `NAME_TYPE`, for the definition at the end and for the module of the type's own events (`SYS_EVENT_MODULE_TYPE(NAME_TYPE)`). Two drivers with one ID fail to link. Drivers live under `components/devices/{soc,chips,virtual}/` and use `runit_device(...)` (`components/devices/device.cmake`, which enforces `WHOLE_ARCHIVE`, no `INCLUDE_DIRS`, no `REQUIRES`, no `device_*` deps, and SoC peripheral drivers under `devices/soc/` only).
- **The struct**: named once, `#define SYS_DEVICE_CTX pca_t`, after the struct and before the first handler. Every handler's body and the hooks take it typed as `c`; no body casts `ctx` or marks a parameter unused.
- **Hooks**: `create(id, c, cfg)`, `suspend(c)`, `resume(c)` are required and typed; a hook of another signature fails to compile in `SYS_DEVICE_TYPE_DEFINE`. `cfg` is the frame's bytes, read in place (a create struct is packed, asserted). `SYS_DEVICE_TYPE_DEFINE_HAS_OP` adds `has_op(c, op)` (an op of the table the device can't run as wired). There is no uninstall hook.
- **The type**: `SYS_DEVICE_TYPE_DEFINE` is the last line. It fills the registry's `sys_device_type_t` (the sizes from the types, the op count from the table) and registers it before `app_main`; a driver never writes that struct.
- **`suspend()`** is complete: after it the device may be removed, and REMOVE runs nothing else of the driver but its resources' releases.
- **Op table**: the type's ops only; SUSPEND, RESUME, REMOVE, INFO and OPS are the registry's and never in it (asserted at boot). RESET is the type's.
- **Own ops** (`0xFFxx`) are a table like a contract's, in the driver's source: `OWN_NAME_OPERATIONS(X, P)` of rows `X(P, OP_NAME, n, key_t, value_t, flags)` and `SYS_DEVICE_OWN_DECLARE(OWN_NAME)`. They get the same handlers, rows and answers (`OP_DECLARE_READ(OWN_DC_MOTOR, OP_SPEED)`, `OP_ENTRY(OWN_DC_MOTOR, OP_SPEED)`, `CMD_REPLY(OWN_DC_MOTOR, OP_SPEED, ...)`), and their flags come from the table: a timer's op is LOCAL there.
- **A payload with a tail** (an op marked `SYS_DEVICE_OP_TAIL`: a head of a fixed size, then bytes): `OP_DECLARE_WRITE_TAIL(P, OP_NAME)`, whose body sees `args` (the head, in place), `tail` and `tail_len`. The table decides: `OP_DECLARE_WRITE` of such an op, and `OP_DECLARE_WRITE_TAIL` of any other, fail to compile. `OP_DECLARE_ON_EVENT` is `OP_DECLARE_EVENT(CONTRACT_EVENT, OP_ON_EVENT)` with the event's length checked. An answer of any length is built with `cmd_append` in an `OP_DECLARE_READ`.
- **Another device's type** is never asked for: a driver asks whether the device has the op it needs (`sys_device_require_op`). Which device a pin is on is the board's configuration: the ESP32-S3's I2C controller takes a pin's index as its GPIO.
- **Events**: a contract's are in its header (`SYS_EVENTS_DECLARE(P, MODULE_ID_<NAME>, DEVICE)`), the type's own in the driver: a table `P_EVENTS(X, P)` of rows `X(P, NAME, code, flags, { payload })` and `SYS_DEVICE_OWN_EVENTS(P)`, whose module is `SYS_EVENT_MODULE_TYPE(P_TYPE)` and lane `DEVICE` (`SYS_EVENT.MD`). Published with `SYS_EVENT_PUBLISH` or `_DEFER`, by what the publishing task can bear.
- **A timer**: `sys_device_timer_init(id, &c->timer, OP)` in `create()`: its expiry is a step of the device (`sys_device_step`, "A step"), under its lock, skipped while the device is busy, suspended or gone. `OP` is the driver's own and LOCAL. A step's handler that needs the time since the last one reads the clock (`board_time_uptime_ms`): steps are skipped, and a driver may run the same step from its other ops.
- **Hardware**: only a driver of the SoC's own peripherals includes ESP-IDF drivers; a chip's driver reaches the hardware through contracts.

## The error policy

[`sys_device_policy`](../sys_device_policy/SYS_DEVICE_POLICY.MD) is beside the registry, which knows nothing of it. Per device: how much its errors matter (importance) and an action per severity.

- **Following the registry**: a CREATED gives a device the default policy, a REMOVED of the same generation takes it away.
- **A reported error**: the app's error sink calls `sys_device_policy_handle(chain)` on the task that reported. Nothing runs there, because that task may be inside a device op and hold the lock the response needs. A chain that needs a response is copied as a record (`ERR_RECORD_MAX`, err_record.h) into the queue of the policy's task; one that needs none is not queued.
- **The policy's task** (`CONFIG_SYS_DEVICE_POLICY_TASK_PRIO` = 14, above the tasks of the event lanes and the router work task) makes the record a chain again and responds: a critical error stops the VM and suspends every device (the sweep), then the device's action for that severity runs. It reports a response's failure itself.
- **No loops**: a chain that holds `ERR_DEV_FAULT_RESPONSE`, and any error reported by the policy's task, is not handled.
- **Full queue**: `ERR_DEV_FAULT_RESPONSE{stage QUEUE}` to the sink; that response does not run.
- A record that is too small for the chain loses its innermost nodes, and the severity may come from them.

## Interceptors and remote devices

- **Interceptor** (`sys_device_set_interceptor(contract, fn, done)`, boot only, one per contract): two stages, both without any lock of the registry; both may call other devices (an ordinary nested call). `fn` sees every call of a contract's ops before the device: it refuses the call or writes the payload the device gets (at most 32 B). `done` (optional) is told how every call `fn` let through ended: the payload the device got and the call's error, NULL when the op worked. What the interceptor decides on is shared by all devices and tasks (a budget), so `fn` decides and takes under a lock of its own module, and `done` gives back what a failed call did not use.
- **Routed call** (`sys_device_call_routed`): the call a command makes; LOCAL ops are refused on every device.
- **Remote device**: CREATE_REMOTE keeps the type and where the device is (`sys_device_remote_t`: board, ID there, WIRED or WIRELESS). Its ops are checked here against the type and sent through the remote hook (`sys_device_set_remote`) after the lock is let go, with the op's flags (a DIRECT read is never served from a copy). `sys_device_find_remote(board, remote_id)` gives the local ID of the remote device that names a device of a board, `sys_device_remote_of(id, &where)` where a remote device is (both a snapshot, no lock): what `sys_device_remote` follows the registry with.

## Limits

- 64 devices, counting buses, mux channels, virtual devices and remote ones: a use mask has one bit per device.
- A slot is about 128 B of static RAM (its lock's storage included), 64 of them whether used or not; the driver's struct is on the heap.
- `CONFIG_SYS_DEVICE_TYPES` (16) types, `CONFIG_SYS_DEVICE_INTERCEPTORS` (2) interceptors.
- The lock timeout must be longer than the slowest op of a device that runs under its lock. An op longer than that is NOLOCK (an I2C bus's SCAN: 112 probes). Under `CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS` (500 ms), single locked ops stay well below the task WDT (5 s).

## Tests

Host tests in `host/` (`python test_host/run.py sys_device` from `runit-esp/`, `HOST_TESTS.MD`): the real slots, registry, call path and remote devices' slots (`remote/sys_device_remote_slot.c`) with the real `router`, `sys_event` and `errors` on the PC, tasks as threads, a lock timeout of 200 ms. The devices are of a test type, `PROBE`, written as a driver is (own ops, own events, a test contract, two resources). `test_sys_device.c`:
- CREATE (INFO, a taken ID, an unknown type, a wrong size, the struct at an odd address), OPS (the registry's and the type's rows, no LOCAL one), a call's error chain (`ERR_DEV_ID`, `ERR_DEV_OP`), an unknown op or side, an ID not found or past the table, a LOCAL op refused as a command and run from C.
- The caller (in `create()`, from an op, from no device), REENTRY, an op's event reaching an inline listener after the outermost call (the listener calls the device whose op published), an event of an op that failed; CREATED / REMOVED with the generation, SUSPENDED with NOTIFY.
- A failed `create()` (its resources last first, no `suspend()`, the use taken back), REMOVE (`suspend()` of a working device only, the releases last first, a failing release), a resource added twice or without its owner's lock; REMOVE of a device in use (`users`), a use closing a cycle (directly, through a third, itself).
- A lock held past the timeout (`ERR_DEV_BUSY` with the op holding it); a step (runs; skipped at once when busy, while suspended, on the next device under the ID; the op's own failure; refused for generation 0, an op not the device's own, a bad ID).
- Sweeps: the order (users first, RESUME the other way, RESET), one event for the sweep, a `suspend()` reaching its dep; the gate (a write from no device and a CREATE refused while SUSPEND runs, a read passes, the gate down after it); a busy device skipped; a sweep from inside an op.
- The interceptor (a clamped payload, a refusal without `done`, `done` after NOT_FOUND and SUSPENDED, one per contract); a remote device (no hook: REMOTE_DOWN, the hook's call, LOCAL refused, a twin, SUSPEND as a flag only, REMOVE); a board device kept by REMOVE and the REMOVE sweep.

Not covered on the host: NOLOCK ops and REMOVE draining them, `has_op`, a sweep's RESET skipping a type without one, the device timer (`sys_device_timer`, the step itself is), priorities and stack sizes.

## Errors (`MODULE_ID_SYS_DEVICE`)

| Tag | Severity | When |
|---|---|---|
| `ERR_DEV_BAD_ARG` | HIGH | A request that is not valid |
| `ERR_DEV_BAD_ID{id}` | HIGH | An ID past `SYS_DEVICE_COUNT` |
| `ERR_DEV_NOT_FOUND{id}` | MEDIUM | No such device, or one being removed |
| `ERR_DEV_ID_TAKEN{id}` | MEDIUM | CREATE on an ID that has a device |
| `ERR_DEV_TYPE_UNKNOWN{type}` | HIGH | No driver registered the type |
| `ERR_DEV_SUSPENDED{id}` | MEDIUM | A write to a suspended device, or one refused by the safe-state sweep |
| `ERR_DEV_NO_MEM{id, size}` | CRITICAL | No memory for the driver's struct |
| `ERR_DEV_UNKNOWN_OP{op}` | HIGH | The device has no such op or side |
| `ERR_DEV_LOCAL_OP{op}` | HIGH | A LOCAL op as a command or on a remote device |
| `ERR_DEV_IN_USE{id, users}` | MEDIUM | REMOVE of a device others use |
| `ERR_DEV_SYSTEM{id}` | MEDIUM | REMOVE of a board device |
| `ERR_DEV_CYCLE{id, dep}` | HIGH | A use that would close a cycle |
| `ERR_DEV_REENTRY{id}` | HIGH | A task called a device from inside its own op |
| `ERR_DEV_BUSY{id, op}` | MEDIUM | The device's lock was not free in time |
| `ERR_DEV_NOT_LOCKED{id}` | HIGH | A slot changed without its lock (a registry bug) |
| `ERR_DEV_REMOTE_DOWN{id, board}` | HIGH | A remote device's board can't be reached |
| `ERR_DEV_REMOTE_DEP{id, dep}` | LOW | A device uses a remote device (allowed, told once) |
| `ERR_DEV_RANGE`, `_BAD_PIN`, `_PIN_LOCKED{id, pin, owner}`, `_PIN_NOT_OWNED{id, pin, owner}` | HIGH | Raised by drivers the same way on every device |
| `ERR_DEV_ID`, `_OP`, `_INSTALL` | NONE | Context of a call or a CREATE |
| `ERR_DEV_FAULT_RESPONSE` | HIGH | The policy's response failed |
