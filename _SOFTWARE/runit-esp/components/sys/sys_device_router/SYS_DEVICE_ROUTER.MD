# sys_device_router — command router for devices (for agents)

Folder `components/sys/sys_device_router/`. Exposes [`sys_device`](../sys_device/SYS_DEVICE.MD) through [`router`](../../core/router/ROUTER.MD) under root key `MODULE_ID_SYS_DEVICE` (`0x20`) without making `sys_device` or drivers depend on `router`.

## Files

| File | Role |
|---|---|
| `sys_device_router.h` | `sys_device_router_hdr_t` (wire header `[dir][id][op]`), `ROUTER_MODULE_DECLARE(sys_device_router)` |
| `sys_device_router.c` | `sys_device_router` implementation and `ROUTER_MODULE_REGISTER(MODULE_ID_SYS_DEVICE, sys_device_router)` |
| `CMakeLists.txt` | Requires `router` and `sys_device`, linked `WHOLE_ARCHIVE` |
| `AGENTS.md` | Rules for changing this folder |

## Wire format

A routed command targets root key `MODULE_ID_SYS_DEVICE` (`0x20`):

```
[0x20][dir u8][id u8][op u16][args ...]
```

`router_route()` consumes the leading key byte (`0x20`) and delivers `[dir][id][op][args...]` to `sys_device_router`:

```c
typedef struct __attribute__((packed)) {
  uint8_t dir;  /**< cmd_dir_e: CMD_DIR_READ / CMD_DIR_WRITE */
  uint8_t id;   /**< Device ID, or SYS_DEVICE_ID_NONE for sweeps */
  uint16_t op;  /**< SYS_DEVICE_OP(contract, n) */
} sys_device_router_hdr_t;  // 4 B, little-endian
```

- Rejects payloads shorter than `sizeof(sys_device_router_hdr_t)` (`4` B) with `ERR_CMD_BAD_LEN`.
- A write of SUSPEND, RESUME, RESET or REMOVE to `SYS_DEVICE_ID_NONE` is a sweep (`sys_device_sweep`): the op on every device, REMOVE leaving the board's own. Any other op to that ID is `ERR_DEV_BAD_ID`.
- Copies the header with `memcpy` (safe on unaligned frames) and forwards the remaining slice to `sys_device_call_routed(h.id, h.dir, h.op, args, out)`.
- `sys_device_call_routed` rejects ops flagged `SYS_DEVICE_OP_LOCAL` with `ERR_DEV_LOCAL_OP` (`MODULE_ID_SYS_DEVICE`), and `CONTRACT_SYS_DEVICE` `REMOVE` rejects board/system devices (`SYS_DEVICE_CREATE_SYSTEM`) with `ERR_DEV_SYSTEM`.

## Decoupling

Drivers and `sys_device` never include `sys_device_router.h` or depend on `router`. Only frame builders (tests, remote forwarding) include `sys_device_router.h`; the firmware pulls `sys_device_router` in via `WHOLE_ARCHIVE` and `ROUTER_MODULE_REGISTER(MODULE_ID_SYS_DEVICE, sys_device_router)`.

## Tests

Covered through `sys_device/host/test_sys_device.c` (`sys_device_call_routed`, local op rejection, unaligned create frames, system device protection) and `core/router/host/test_router.c` (root dispatch).
