# sys_device_event — device event subscriptions and `EVENT` contract (for agents)

Folder `components/sys/sys_device_event/`. Bridges [`sys_event`](../sys_event/SYS_EVENT.MD) and [`sys_device`](../sys_device/SYS_DEVICE.MD) so neither depends on the other: defines the `EVENT` contract (`0x03`), helper macros for event-receiving device ops, and generation-checked event subscriptions that deliver into a device op.

## Files

| File | Role |
|---|---|
| `sys_device_event.h` | `CONTRACT_EVENT` (`0x03`), `CONTRACT_EVENT_OPERATIONS` (`OP_ON_EVENT`), `OP_DECLARE_EVENT`, `OP_DECLARE_ON_EVENT`, `sys_device_event_subscribe_op`, `sys_device_event_subscribe` |
| `sys_device_event_id.h` | `MODULE_ID_DECLARE(MODULE_ID_EVENT, 0x03)` |
| `sys_device_event.c` | The subscription adapter (`sys_device_target_pack`/`unpack` + `sys_device_deliver`) |
| `contract_event.json` | Descriptor of the `EVENT` contract (`OP_ON_EVENT`): the 20 B `sys_event_t` and its payload (`services.test.ts` of the app holds it against the C struct) |
| `CMakeLists.txt` | Requires `sys_device` and `sys_event` |
| `AGENTS.md` | Rules for changing this folder |

## Contract `EVENT` (`0x03`)

`MODULE_ID_DECLARE(MODULE_ID_EVENT, 0x03)` (`sys_device_event_id.h`) / `SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_EVENT, MODULE_ID_EVENT)`.

| Op | Dir | Args (wire) | Answer | Flags | Purpose |
|---|---|---|---|---|---|
| `0x00 OP_ON_EVENT` | W | `sys_event_t` (20 B) + tail (`event->len` B) | — | `SYS_DEVICE_OP_ALWAYS \| SYS_DEVICE_OP_TAIL` | Deliver a subscribed event to a device |

- `OP_ON_EVENT` is marked `SYS_DEVICE_OP_ALWAYS`, so it also reaches suspended devices; a handler checks `sys_device_self_suspended()` when it should do nothing while suspended.
- No events of its own (`EVENT` only receives).

### Driver handler macros

```c
OP_DECLARE_ON_EVENT {
  if (sys_device_self_suspended()) { return NULL; }
  if (event->key.tag == IO_EV_FALLING) { return read_inputs(c); }
  return NULL;
}
```

- `OP_DECLARE_EVENT(P, name)` expands to `OP_DECLARE_WRITE_TAIL(P, name)`, checks `tail_len == args->len` (`ERR_CMD_BAD_LEN` otherwise, with `sizeof(sys_event_t) + tail_len` / `+ args->len`), and calls `static err_h P##_w_##name##_event(SYS_DEVICE_CTX* c, const sys_event_t* event)`.
- Used for both `CONTRACT_EVENT` (`OP_DECLARE_ON_EVENT`, paired with `OP_ENTRY_W(CONTRACT_EVENT, OP_ON_EVENT)`) and a driver's own event ops (paired with `SYS_DEVICE_CONTRACT_OWN` and `SYS_DEVICE_OP_LOCAL | SYS_DEVICE_OP_TAIL`).

## Subscription API

```c
MUST_USE err_h sys_device_event_subscribe_op(sys_event_key_t filter, uint8_t device, uint16_t op, uint16_t* handle);
MUST_USE err_h sys_device_event_subscribe(sys_event_key_t filter, uint8_t device, uint16_t* handle);
```

- `sys_device_event_subscribe` is shorthand for `op = CONTRACT_EVENT_OP_ON_EVENT`.
- `sys_device_event_subscribe_op` targets either `CONTRACT_EVENT_OP_ON_EVENT` or an own op (`(op >> 8) == SYS_DEVICE_CONTRACT_OWN`), otherwise `ERR_EVENT_BAD_ARG`.
- Validates `device < SYS_DEVICE_COUNT` and reads `gen = sys_device_gen(device)` (`gen != 0`, else `ERR_EVENT_BAD_ARG`).
- Packs `[op u16][gen u8][owner u8]` into `void* ctx` (`_Static_assert(sizeof(uintptr_t) >= sizeof(uint32_t))`) and registers a `SYS_EVENT_RUN_INLINE` listener in `sys_event`.
- On match, the listener invokes `sys_device_deliver(owner, gen, op, event, sizeof(*event) + event->len)`:
  - waits for `owner`'s device lock;
  - runs `ALWAYS` ops even if suspended;
  - silently returns `NULL` if the device was removed or recreated with a new generation.
- Unsubscribe via `sys_event_unsubscribe(handle)` (or automatically via `sys_io_claim` when claiming an interrupt pin).

## Tests

No dedicated host suite in this folder; `sys_device_deliver` (lock wait, `ALWAYS`, generation mismatch) is tested in `sys_device/host/test_sys_device.c`, and inline subscription mechanics in `sys_event/host/test_sys_event.c`.
