# System Buffer Manager (sys_buffers)

IMPORTANT: If you modify the source files in this component (sys_buffers.h or sys_buffers.c), you must update this documentation to reflect the changes.

## Overview
The `sys_buffers` component provides a variable-length item ring buffer for physical transmission channels (such as BLE notifications/indications or serial interfaces). It wraps a single FreeRTOS `RINGBUF_TYPE_NOSPLIT` ringbuffer per descriptor - every push/pop moves one whole item, so message boundaries are never split or coalesced.

### Directory Map
```
components/system/sys_buffers/
├── CMakeLists.txt
├── SYS_BUFFERS.MD       # This documentation
├── sys_buffers.c        # Buffer allocation and item push/pop implementation
├── include/
│   └── sys_buffers.h       # sys_buff_t descriptor struct and function prototypes
└── errors/
    └── sys_error_buffers.h # SYS_BUFFERS_OWNER_MAP / SYS_ERROR_BUFFERS_MAP (see [[SYS_ERRORS.MD]])
```

---

## Architecture & Concept

Data flow:
```
Push: caller -> sys_buff_push() -> Ringbuffer (FreeRTOS, RINGBUF_TYPE_NOSPLIT)
Pop:  Ringbuffer -> sys_buff_pop() -> Item [0..N-1] -> Output Buffer
```

Every item is pushed and popped whole. An item that doesn't fit in the destination buffer on pop (`max_size` too small) is **dropped and reported** (`ERR_BUFFERS_ITEM_TOO_LONG`), never truncated: a frame cut short could otherwise be decoded as a valid shorter one. It is never split across two pops either.

---

## Core Data Structures

### Buffer Descriptor (sys_buff_t)
```c
typedef struct {
  RingbufHandle_t buff;  // FreeRTOS Ringbuffer handle (RINGBUF_TYPE_NOSPLIT)
} sys_buff_t;
```

---

## Manager API Functions

### Allocation & Initialization APIs
* **`sys_buff_init(sys_buff_t* buff, size_t size)`**:
  Allocates the underlying FreeRTOS ringbuffer memory for a `sys_buff_t` descriptor. Returns `NULL` on success, or `ERR_BASE_NO_MEM`/`ERR_INVALID_VAL_UI32` on failure.
* **`sys_buff_free(sys_buff_t* buff)`**:
  Frees the underlying FreeRTOS ringbuffer memory and resets `buff->buff = NULL`.

A statically allocated ringbuffer (`R_RINGBUFFER_DEFINE`) can be wrapped directly: `(sys_buff_t){.buff = handle}` (sys_interface's tap buffer does this).

### Sizing: think in items, not bytes
A no-split ESP ringbuffer stores each item rounded up to 4 bytes behind an 8-byte header (`SYS_BUFF_ITEM_HDR_LEN`), and **accepts one item of at most half its size minus that header**. A 512-byte buffer takes items of only 248 bytes. Size buffers with:

* **`SYS_BUFF_SIZE_FOR(item_max, count)`**: bytes needed for `count` items of up to `item_max` bytes (at least 2 items, since one item may use only half). Transport buffers use `SYS_BUFF_SIZE_FOR(CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX, n)`.
* **`sys_buff_max_item(buff)`**: the largest item an existing buffer accepts (`xRingbufferGetMaxItemSize`); BLE reports it as part of a characteristic's frame limit.

### Payload APIs
* **`sys_buff_push(sys_buff_t* buff, const void* data, size_t len, uint32_t wait_ms)`**:
  Enqueues one variable-length item. Returns `ERR_BASE_NO_MEM` if the item doesn't fit within `wait_ms`.
* **`sys_buff_pop(sys_buff_t* buff, uint8_t* buffer, size_t max_size, size_t* out_len)`**:
  Dequeues one item verbatim. Returns `ERR_BASE_NOT_FOUND` if empty, `ERR_BUFFERS_ITEM_TOO_LONG` (item dropped, `*out_len = 0`) if it is longer than `max_size`, `ERR_INVALID_VAL_UI32` if `max_size < 1`.
* **`sys_buff_clear(sys_buff_t* buff)`**:
  Removes every queued item.

---

## Error Handling
`sys_buffers.c` tags every error it raises with function-specific owner tags declared in `sys_error_buffers.h` (`SYS_BUFFERS_OWNER_MAP`, ID range `0xA700`–`0xA705`):
* `OWNER_SYS_BUFFERS_BASE` (`0xA700`): Module base owner.
* `OWNER_SYS_BUFF_INIT` (`0xA701`): `sys_buff_init()` owner.
* `OWNER_SYS_BUFF_FREE` (`0xA702`): `sys_buff_free()` owner.
* `OWNER_SYS_BUFF_PUSH` (`0xA703`): `sys_buff_push()` owner.
* `OWNER_SYS_BUFF_POP_FRAMED` (`0xA704`): unused (the framed pop was removed; the ID stays reserved).
* `OWNER_SYS_BUFF_POP_RAW` (`0xA705`): `sys_buff_pop()` owner.

| Tag | Payload | Raised when |
| :--- | :--- | :--- |
| `ERR_BUFFERS_ITEM_TOO_LONG` (`0xA701`, MEDIUM) | `uint32_t len; uint32_t max;` | `sys_buff_pop()` found an item longer than the destination; the item was dropped. |

Otherwise it raises base tags (`ERR_INVALID_VAL_UI32`, `ERR_BASE_NO_MEM`, `ERR_BASE_NOT_FOUND`).
