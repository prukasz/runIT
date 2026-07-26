# BLE Manager (sys_ble)

IMPORTANT: If you modify the source files in this component (sys_ble.h, sys_ble_priv.h, sys_ble.c, sys_ble_stack.c, or sys_error_ble.h), you must update this documentation to reflect the changes.

## Overview
The `sys_ble` component wraps the NimBLE host stack behind a declarative GATT registry: services and characteristics are described as config structs and appended to an internal linked-list registry, then *compiled* into real NimBLE `ble_gatt_svc_def`/`ble_gatt_chr_def` structures only when `sys_ble_database_sync()` runs. TX is non-blocking (ring buffers drained by a dedicated task, via `sys_buffers`), RX can be either polled or delivered by callback, and a characteristic can be added to (or removed from) a service that's already live — the registry tracks what changed and recompiles only the affected service.

### Directory Map
```
components/system/ble/
├── CMakeLists.txt
├── SYS_BLE.MD             # This documentation
├── sys_ble_priv.h         # Private registry structs, shared between sys_ble.c and sys_ble_stack.c
├── sys_ble.c              # Public API: registry management, TX enqueue/RX dequeue, database sync
├── sys_ble_stack.c        # NimBLE glue: GAP/GATT event handlers, compile-to-NimBLE, TX worker task
└── include/
    ├── sys_ble.h          # Public config structs, channel API, and function declarations
    └── sys_error_ble.h    # sys_ble owner map and ERR_BLE_* error tag map
```

---

## Architecture & Concept

### Registry → Compile Model
Nothing here is a static NimBLE array. `sys_ble_service_create()`/`sys_ble_char_create()` just append `sys_ble_svc_node_t`/`sys_ble_char_node_t` nodes (`sys_ble_priv.h`) to `g_ble_ctx.services`. `sys_ble_database_sync()` is the compile step that turns the current registry state into real `struct ble_gatt_svc_def`/`ble_gatt_chr_def` arrays (`compile_chars`/`populate_svc_def`, `sys_ble_stack.c`) and registers them with NimBLE. It must be called after any registry change to take effect — low-level callers batch several creates then call it once; the unified channel API (below) can do this automatically.

### Dynamic Characteristics (add/remove on a live service)
NimBLE's own dynamic-registration primitives (`ble_gatts_add_dynamic_svcs`/`ble_gatts_delete_svc`) work at **service granularity only** — there's no NimBLE API to add a single characteristic to an already-registered service. `sys_ble` bridges this with per-node dirty tracking:
* `sys_ble_svc_node_t.registered` — true once the service is live in NimBLE.
* `sys_ble_svc_node_t.dirty` — true when a characteristic was added/removed under an already-`registered` service.
* `sys_ble_char_node_t.pending_add` / `pending_remove` — per-characteristic bookkeeping; `pending_remove` defers the actual free until the service is safely recompiled (NimBLE may still reference the node's `val_handle`/`arg` until then).

`sys_ble_database_sync()` handles three cases per service:
1. **Not yet registered** — compile + `ble_gatts_add_dynamic_svcs()` (or, on the very first sync, the bulk `ble_gatts_add_svcs()` path via `sys_ble_stack_init()`).
2. **Registered, not dirty** — skip, nothing changed.
3. **Registered, dirty** — `ble_gatts_delete_svc()` the live service, free the old compiled def, drop any `pending_remove` nodes, recompile the *entire current* characteristic list, `ble_gatts_add_dynamic_svcs()` it again, then call `ble_svc_gatt_changed()` so already-subscribed/bonded clients know to re-discover. If the re-add fails, the service is marked unregistered so the next `sync()` retries it from scratch rather than getting stuck half-deleted.

`sys_ble_char_create()`/`sys_ble_char_remove()` set the dirty/pending flags but never touch NimBLE directly — only `sys_ble_database_sync()` does.

### Two RX Delivery Modes
* **Poll** — the peer's write lands in the characteristic's dedicated RX buffer (`rx_buff`, a `sys_buff_t` - one GATT write is one item, so a single `sys_ble_char_rx_dequeue()` call always returns exactly one peer write, never a partial or coalesced one) and its binary semaphore (`rx_sem`) is signaled; the app drains it with `sys_ble_char_rx_dequeue()` on its own schedule/task. Rather than hand-rolling that task, call `sys_interface_bind_ble_rx(char_uuid, max_frame_len)` ([[SYS_INTERFACE.MD]]) — it spawns the pump, blocks on `rx_sem`, and feeds every frame to `sys_interface_decode()`. Simple, latency-tolerant, no callback infrastructure needed.
* **Callback** — if the characteristic node has an `rx_handler` (`own_func_t`) set, every successful write also dispatches it via `SYS_CB_OWN()` — i.e. through the *existing* `CALLBACK_OWN_FUNC` path already handled by `sys_cb_task` in [[SYS_CALLBACKS.MD]], with zero changes needed to the callback dispatcher. Lower latency, no hand-rolled polling task required; the handler still calls `sys_ble_char_rx_dequeue()` itself to get the bytes (the callback is a wakeup, not a data-carrying event) and typically hands them to `sys_interface_decode()`.

### Asynchronous TX Slots
Up to `MAX_TX_BUFFERS` (3) TX ring buffers can be assigned per characteristic, each identified by its configured header byte (there is no separate slot ID - the header doubles as the key). `sys_ble_char_send()` enqueues into the matching slot (non-blocking) and wakes the dedicated `sys_ble_task_func` task, which flattens all active slots (`sys_ble_rebuild_active_tx_slots()`), dequeues via `sys_buff_pop_framed()` (prepends the slot's configured header byte), and sends via `ble_gatts_notify_custom`/`ble_gatts_indicate_custom` with retry-on-`BLE_HS_ENOMEM` backoff. Every TX/RX buffer in `sys_ble` is a variable-length item buffer (`sys_buff_t` from [[SYS_BUFFERS.MD]]) - a message is always pushed and popped whole, never split or auto-packed.

### GATT Reads
A characteristic with `is_read` set serves back whatever was last sent through it: `try_send_slot()` snapshots the framed bytes (post header-prepend, i.e. exactly what went out over the air) into the node's `last_val`/`last_val_len` after every successful TX. `sys_ble_gatt_access_cb()`'s `BLE_GATT_ACCESS_OP_READ_CHR` branch serves that snapshot. A characteristic that's never been sent on reads as empty. There is no app-supplied "getter" mechanism — reads only ever mirror the last notified/indicated value.

### Stack-Level Callback Events
`SYS_BLE_CB(event_id, event_value, mask)` posts a `CALLBACK_BLE` event for connect/disconnect/failure (`sys_ble_events_e`), routed through `sys_ble_add_callback()`'s stored `route_mask`. `sys_cb_task` currently only logs these (skeleton dispatch, same as `CALLBACK_IO`/`CALLBACK_PWR` — see [[SYS_CALLBACKS.MD]]); no real per-route subscriber table exists yet for any of the three.

---

## Core Data Structures & Enums

### Service / Characteristic Config
```c
typedef struct { uint16_t uuid; bool is_primary; } sys_ble_svc_cfg_t;

typedef struct {
  uint16_t uuid;
  bool is_read, is_write, is_indicate, is_notify;
  const char* desc;  // Optional GATT descriptor 0x2901
} sys_ble_char_cfg_t;

typedef struct {
  sys_ble_char_cfg_t info;
  size_t rx_buffer_size;  // 0 if write/notify is disabled
} sys_ble_char_create_t;
```

### TX Buffer Config
```c
typedef struct {
  uint8_t header;      // stream tag; also identifies this TX slot within its characteristic
  size_t size;          // ring buffer capacity in bytes
  bool is_indication;
} sys_ble_tx_buf_cfg_t;
```
`sys_ble_char_assign_tx_buffer()` allocates the underlying `sys_buff_t` (from [[SYS_BUFFERS.MD]]) internally - callers never touch a ring buffer handle directly.

### Unified Channel Config (sys_ble_channel_cfg_t)
```c
typedef enum { SYS_BLE_RX_MODE_NONE, SYS_BLE_RX_MODE_POLL, SYS_BLE_RX_MODE_CALLBACK } sys_ble_rx_mode_e;

typedef struct {
  uint16_t svc_uuid;              // 0 / SYS_BLE_SVC_DEFAULT_AUTO to use the lazily-created default service
  sys_ble_char_cfg_t chr;
  size_t rx_buffer_size;
  sys_ble_rx_mode_e rx_mode;
  own_func_t rx_handler;           // used only when rx_mode == SYS_BLE_RX_MODE_CALLBACK
  const sys_ble_tx_buf_cfg_t* tx_bufs;
  uint8_t tx_buf_count;
} sys_ble_channel_cfg_t;
```

### BLE Status & Events
```c
typedef struct { bool is_connected; uint16_t mtu_size; uint32_t rx_overflow_count; } sys_ble_status_t;

typedef enum sys_ble_events_e { SYS_BLE_EVENT_CONNECT = 0, SYS_BLE_EVENT_DISCONNECT, SYS_BLE_EVENT_FAILURE, SYS_BLE_EVENT_MAX } sys_ble_events_e;
```

---

## Utility Macros

* **`SYS_BLE_CB(event_id, event_value, mask)`**: Constructs a `cb_event_t` with `CALLBACK_BLE` type and triggers it via `sys_callback_trigger` — used only for stack-level events (connect/disconnect/failure), not per-characteristic data.

---

## Manager API Functions

### Unified Channel API (declarative — prefer this for new code)
* **`sys_ble_channel_create(cfg, sync_now)`**: Collapses service-reuse-or-create + `sys_ble_char_create` + N × `sys_ble_char_assign_tx_buffer` + RX-handler registration into one call. Reuses an existing service if `cfg->svc_uuid` already exists (unlike the raw `sys_ble_service_create()`, which errors on a duplicate); lazily creates the reserved default service (`SYS_BLE_SVC_DEFAULT_AUTO = 0xFEFE`) if `svc_uuid` is 0. Pass `sync_now = true` for a single dynamic post-boot addition, or `false` to batch several calls followed by one explicit `sys_ble_database_sync()` — mirrors how `runit_board_cfg.h`'s `sys_ble_static_config()` already batches several low-level calls before one sync.

  Example — collapsing what today takes ~10 low-level calls (see `runit_board_cfg.h`'s `sys_ble_static_config()`) into one:
  ```c
  sys_ble_channel_cfg_t chan = {
    .svc_uuid = SYS_BLE_SVC_RUNIT,
    .chr = {.uuid = MY_CHR_UUID, .is_write = true, .is_notify = true, .desc = "my channel"},
    .rx_buffer_size = 512,
    .rx_mode = SYS_BLE_RX_MODE_CALLBACK,
    .rx_handler = {.own_func = my_rx_handler, .device_handle = NULL},
    .tx_bufs = &(sys_ble_tx_buf_cfg_t){.header = 0, .size = 1024},
    .tx_buf_count = 1,
  };
  SE_ORIGIN_CALL(sys_ble_channel_create(&chan, true));
  ```
  where `my_rx_handler` typically just dequeues and decodes:
  ```c
  err_h my_rx_handler(void* device_handle, struct cb_event_t* event) {
    uint8_t buf[512]; size_t len;
    sys_ble_char_rx_dequeue(MY_CHR_UUID, buf, sizeof(buf), &len);
    if (len) return sys_interface_decode(buf, len);
    return NULL;
  }
  ```

### Low-Level APIs (manual control — existing callers, still fully supported)
* **`sys_ble_init()`**: Idempotent manager init.
* **`sys_ble_add_callback(on_event, route_mask)`**: Registers a route mask for a stack-level `sys_ble_events_e`.
* **`sys_ble_service_create(cfg)`** / **`sys_ble_service_remove(svc_uuid)`**: Declare/remove a GATT service; remove deletes it live from NimBLE immediately if already registered.
* **`sys_ble_char_create(svc_uuid, cfg)`** / **`sys_ble_char_remove(svc_uuid, char_uuid)`**: Declare/remove a characteristic under a service. If the parent service is already live, these mark it `dirty` (create) or defer the free via `pending_remove` (remove) rather than touching NimBLE directly — the next `sys_ble_database_sync()` call does the actual recompile.
* **`sys_ble_char_assign_tx_buffer(char_uuid, buf_cfg)`**: Allocates a TX buffer slot keyed by `buf_cfg->header` (up to `MAX_TX_BUFFERS` per characteristic).
* **`sys_ble_char_get_rx_semaphore(char_uuid, out_sem)`**: Fetches the RX semaphore handle for poll-mode consumers. The handle is non-NULL exactly when the characteristic was created with `rx_buffer_size > 0`, so it doubles as a non-destructive "does this characteristic accept RX?" probe.
* **`sys_ble_char_rx_dequeue(char_uuid, buffer, max_len, out_len)`**: Pulls one item (one peer write) from the characteristic's RX buffer, via `sys_buff_pop_raw()`.
* **`sys_ble_char_rx_inject(char_uuid, data, len)`**: Test/debug utility — pushes raw bytes into a characteristic's RX buffer and signals `rx_sem`, the exact same push-then-signal path `sys_ble_gatt_access_cb()` takes for a real peer write. Lets you exercise the full RX pipeline (buffer, semaphore, `sys_interface_bind_ble_rx()` pump) from firmware code with no BLE peer connected.
* **`sys_ble_char_send(char_uuid, header, data, len, return_when_full)`**: Enqueues data into the TX slot identified by `header` for the background task to notify/indicate; also becomes the value served on a subsequent GATT read.
* **`sys_ble_database_sync()`**: Compiles and (re)registers all pending registry changes with NimBLE — see the three-case dispatch above. Must be called after any create/remove.
* **`sys_ble_get_status(out_status)`**: Connection state, MTU, RX overflow count.

---

## Error Ownership

Every function sets `#undef OWNER` / `#define OWNER OWNER_SYS_BLE_<FUNCTION>` immediately before its body, per the [[runit]] skill's owner-tagging convention. See `sys_error_ble.h` for the full `SYS_BLE_OWNER_MAP` (includes `OWNER_SYS_BLE_CHANNEL_CREATE` for the unified API) and `SYS_ERROR_BLE_MAP` tags: `ERR_BLE_STACK_FAILED` (host/init failures, via `CHECK_BLE_CALL`), `ERR_BLE_ADV_FAILED`, `ERR_BLE_GATT_FAILED` (NimBLE service add/delete/recompile failures), and `ERR_BLE_HARDWARE_FAULT` (reserved, currently unused).
