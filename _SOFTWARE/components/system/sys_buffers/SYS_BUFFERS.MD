# System Buffer Manager (sys_buffers)

IMPORTANT: If you modify the source files in this component (sys_buffers.h or sys_buffers.c), you must update this documentation to reflect the changes.

## Overview
The `sys_buffers` component provides standard ring-buffer payload formatting, initialization, and extraction tools for physical transmission channels (such as BLE notifications/indications or serial interfaces). It wraps FreeRTOS Ringbuffer primitives, automatically prepending protocol header bytes and packing items into output array buffers for network stack consumption.

### Directory Map
```
components/system/sys_buffers/
├── CMakeLists.txt
├── SYS_BUFFERS.MD       # This documentation
├── sys_buffers.c        # Buffer allocation, payload extraction, and item packing implementation
└── include/
    └── sys_buffers.h    # sys_tx_buff_t descriptor struct, initializer macros, and function prototypes
```

---

## Architecture & Concept

Data packaging flow:
```
Ringbuffer (FreeRTOS) -> sys_buff_prepare_tx() -> Header byte [0] + Data Payload [1..N] -> Output Buffer
```

### Buffer Extraction Modes
1. **`RINGBUF_TYPE_NOSPLIT` Mode (Dynamic Variable-Size Items)**:
   - Fetches a single complete payload item from the ring buffer using `xRingbufferReceive`.
   - Writes `tx_buff->header` byte to `buffer[0]`.
   - Copies item payload bytes into `buffer[1]`.
   - Automatically truncates items exceeding `max_size - 1` bytes and logs a warning.
2. **`RINGBUF_TYPE_BYTEBUF` Mode (Auto-Packing Fixed-Size Items)**:
   - Calculates `max_bytes` aligned to multiples of `const_item_size` that can fit within `max_size - 1`.
   - Pulls contiguous chunks up to `max_bytes` using `xRingbufferReceiveUpTo`.
   - Writes `tx_buff->header` byte to `buffer[0]` and appends consecutive item payloads starting at `buffer[1]`.

---

## Core Data Structures & Initializer Macros

### Transmit Buffer Descriptor (sys_tx_buff_t)
```c
typedef struct sys_tx_buff_t {
  RingbufHandle_t buff;       // FreeRTOS Ringbuffer handle
  RingbufferType_t type;      // RINGBUF_TYPE_NOSPLIT or RINGBUF_TYPE_BYTEBUF
  size_t const_item_size;     // Fixed size per item (used for BYTEBUF packing)
  uint8_t header;             // Standard packet header byte prepended to output
} sys_tx_buff_t;
```

### Initializer Macros
* **`SYS_BUF_FIXED_ITEM(header, item_size)`**:
  Constructs a compound literal `sys_tx_buff_t` configured for auto-packing fixed-size payload items (`RINGBUF_TYPE_BYTEBUF`).
* **`SYS_BUF_DYNAMIC_ITEM(header)`**:
  Constructs a compound literal `sys_tx_buff_t` configured for variable-size payload items (`RINGBUF_TYPE_NOSPLIT`).

---

## Manager API Functions

### Allocation & Initialization APIs
* **`sys_buff_init(sys_tx_buff_t* tx_buff, size_t size)`**:
  Allocates the underlying FreeRTOS ringbuffer memory for a `sys_tx_buff_t` descriptor. Returns `STA_OK` on success, or `ERR_NO_MEM` on failure.
* **`sys_buff_tx_init(sys_tx_buff_t item, size_t size)`**:
  Value-returning one-liner initialization helper. Allocates and returns a populated `sys_tx_buff_t` struct.
* **`sys_buff_free(sys_tx_buff_t* tx_buff)`**:
  Frees the underlying FreeRTOS ringbuffer memory and resets `buff = NULL`.

### Payload Formatting APIs
* **`sys_buff_prepare_tx(tx_buff, buffer, max_size, out_len)`**:
  Dequeues data from `tx_buff->buff`, inserts `tx_buff->header` as the leading byte in `buffer[0]`, copies payload data into `buffer[1..out_len-1]`, and returns `STA_OK`. Returns `ERR_NOT_FOUND` if the ring buffer is empty, or `ERR_INVALID_SIZE` if `max_size < 2`.
