# System Buffer Manager (sys_buffers)

IMPORTANT: If you modify the source files in this component (sys_buffers.h or sys_buffers.c), you must update this documentation to reflect the changes.

## Overview
The `sys_buffers` component provides a variable-length item ring buffer for physical transmission channels (such as BLE notifications/indications or serial interfaces). It wraps a single FreeRTOS `RINGBUF_TYPE_NOSPLIT` ringbuffer per descriptor - every push/pop moves one whole item, so message boundaries are never split or coalesced - and optionally prepends a protocol header byte on dequeue.

### Directory Map
```
components/system/sys_buffers/
├── CMakeLists.txt
├── SYS_BUFFERS.MD       # This documentation
├── sys_buffers.c        # Buffer allocation and item push/pop implementation
└── include/
    ├── sys_buffers.h       # sys_buff_t descriptor struct and function prototypes
    └── sys_error_buffers.h # SYS_BUFFERS_OWNER_MAP / SYS_ERROR_BUFFERS_MAP (see [[SYS_ERRORS.MD]])
```

---

## Architecture & Concept

Data flow:
```
Push: caller -> sys_buff_push() -> Ringbuffer (FreeRTOS, RINGBUF_TYPE_NOSPLIT)
Pop (framed):  Ringbuffer -> sys_buff_pop_framed() -> Header byte [0] + Item [1..N] -> Output Buffer
Pop (raw):     Ringbuffer -> sys_buff_pop_raw()    -> Item [0..N-1]                 -> Output Buffer
```

Every item is pushed and popped whole. An item that doesn't fit in the destination buffer on pop (`max_size` too small) is truncated and counted in `sys_buff_t.truncated` - it is never split across two pops.

---

## Core Data Structures

### Buffer Descriptor (sys_buff_t)
```c
typedef struct {
  RingbufHandle_t buff;       // FreeRTOS Ringbuffer handle (RINGBUF_TYPE_NOSPLIT)
  uint8_t header;              // Prepended to frames by sys_buff_pop_framed()
  uint32_t truncated;          // Count of items truncated on pop
} sys_buff_t;
```

---

## Manager API Functions

### Allocation & Initialization APIs
* **`sys_buff_init(sys_buff_t* buff, uint8_t header, size_t size)`**:
  Allocates the underlying FreeRTOS ringbuffer memory for a `sys_buff_t` descriptor and records its header byte. Returns `NULL` on success, or `ERR_NO_MEM`/`ERR_INVALID_VAL_UI32` on failure.
* **`sys_buff_free(sys_buff_t* buff)`**:
  Frees the underlying FreeRTOS ringbuffer memory and resets `buff->buff = NULL`.

### Payload APIs
* **`sys_buff_push(sys_buff_t* buff, const void* data, size_t len, uint32_t wait_ms)`**:
  Enqueues one variable-length item. Returns `ERR_NO_MEM` if the item doesn't fit within `wait_ms`.
* **`sys_buff_pop_framed(sys_buff_t* buff, uint8_t* buffer, size_t max_size, size_t* out_len)`**:
  Dequeues one item, writing `buff->header` to `buffer[0]` and the item payload to `buffer[1..out_len-1]`. Returns `ERR_NOT_FOUND` if empty, `ERR_INVALID_VAL_UI32` if `max_size < 2`.
* **`sys_buff_pop_raw(sys_buff_t* buff, uint8_t* buffer, size_t max_size, size_t* out_len)`**:
  Dequeues one item verbatim (no header byte). Returns `ERR_NOT_FOUND` if empty, `ERR_INVALID_VAL_UI32` if `max_size < 1`.

Both pop functions truncate (and count in `buff->truncated`) an item that exceeds the space available in `buffer` rather than failing or splitting it across calls.

---

## Error Handling
`sys_buffers.c` tags every error it raises with function-specific owner tags declared in `sys_error_buffers.h` (`SYS_BUFFERS_OWNER_MAP`, ID range `0xA700`–`0xA705`):
* `OWNER_SYS_BUFFERS_BASE` (`0xA700`): Module base owner.
* `OWNER_SYS_BUFF_INIT` (`0xA701`): `sys_buff_init()` owner.
* `OWNER_SYS_BUFF_FREE` (`0xA702`): `sys_buff_free()` owner.
* `OWNER_SYS_BUFF_PUSH` (`0xA703`): `sys_buff_push()` owner.
* `OWNER_SYS_BUFF_POP_FRAMED` (`0xA704`): `sys_buff_pop_framed()` owner.
* `OWNER_SYS_BUFF_POP_RAW` (`0xA705`): `sys_buff_pop_raw()` owner.

It currently raises base error tags (`ERR_INVALID_VAL_UI32`, `ERR_BASE_NO_MEM`, `ERR_BASE_NOT_FOUND`); `SYS_ERROR_BUFFERS_MAP` is reserved for future buffer-specific error payload structures.
