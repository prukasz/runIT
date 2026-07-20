# Status & Error Handling (status)

IMPORTANT: If you modify the source files in this component (status.h, status.c, status_codes.h, status_codes.c, or any code mappings), you must update this documentation to reflect the changes.

## Overview
The status component implements a unified status reporting and error propagation mechanism throughout the runIT firmware. It uses a structured report token (`status_rep_t`) to aggregate error codes, originating modules (owners), debug payloads, and severity levels. This structure enables local recovery policies, diagnostic logging, and remote status transmission.

### Directory Map
```
components/status/
├── CMakeLists.txt
├── STATUS.MD             # This documentation
├── status.c              # Queue transmission and reporting control
├── include/
│   └── status.h          # Core structures, checking macros, and API declarations
└── codes/
    ├── devices_owners.h  # Device driver owners and error mapping declarations
    ├── status_codes.c    # Conversion helpers (names and strings)
    ├── status_codes.h    # Master status maps (Global Error & Owner maps) and enums
    └── sys/
        └── sys_status_codes.h # System submodule specific owners and error code maps
```

---

## Core Data Structures

### status_rep_t
This 16-byte struct is passed by value to propagate error state.
```c
typedef struct {
  uint32_t e_code;      // Error identifier (from status_code_e enum, prefixed with ERR_)
  uint32_t e_owner;     // Origin module identifier (from status_owner_e enum, prefixed with OWNER_)
  uint64_t payload;     // 64-bit debug payload (context-specific, e.g. raw error codes, addresses, values)
  struct {
    uint8_t severity : 2;     // 0: STATUS_INFO, 1: STATUS_WARNING, 2: STATUS_CRITICAL
    uint8_t payload_type : 6; // Presets format parsing (e.g. STATUS_PAYLOAD_SYS_IO, STATUS_PAYLOAD_ESP_ERR)
  } details;
} status_rep_t;
```

### Severity Levels
* `STATUS_INFO` (0)
* `STATUS_WARNING` (1)
* `STATUS_CRITICAL` (2)

### Payload Types
* `STATUS_PAYLOAD_UNKNOWN` (0)
* `STATUS_PAYLOAD_DEVICE` (1)
* `STATUS_PAYLOAD_SYS_IO` (2)
* `STATUS_PAYLOAD_ESP_ERR` (3)
* `STATUS_PAYLOAD_VM` (4)

---

## Implicit OWNER Definition
All diagnostic checking macros implicitly refer to the macro `OWNER`. **Every `.c` source file using these macros must define its local OWNER before calling them.**
```c
#undef OWNER
#define OWNER OWNER_DEVICE_GPIO_ESP
```

---

## Macro Naming Convention
Status check macros follow a standard postfix notation:
* **`STA`**: Actions returning or evaluating `status_rep_t`.
* **`R`**: Return status to the caller.
* **`P`**: Push status into the remote transmission queue.
* **`RP`**: Push status into the transmission queue AND return it.
* **`C` / `W` / `I`**: Critical, Warning, and Info severity levels.

---

## Utility Macros

### Struct Initializers & Queries
* **`STA_OK`**: Evaluates to `(status_rep_t){0}` representing a successful execution state.
* **`STA_C(code, owner, payload, payload_type)`**: Direct inline constructor for critical severity status reports.
* **`STA_W(code, owner, payload, payload_type)`**: Direct inline constructor for warning severity status reports.
* **`STA_I(code, owner, payload, payload_type)`**: Direct inline constructor for info severity status reports.
* **`STA_IS_OK(status)`**: Returns `true` if `status.e_code == 0`.
* **`STA_IS_ERR(status)`**: Returns `true` if `status.e_code != 0` and severity is warning or critical.

### Propagation
* **`STA_P(status)`**: Pushes `status` into the transmission stream.
* **`STA_RP(status)`**: Pushes `status` into the transmission stream if it represents an error, then returns it.
* **`STA_R_ON_ERR(status)`**: Evaluates `status`. If it is an error, returns it immediately (does not push).
* **`STA_P_ON_ERR(status)`**: Evaluates `status`. If it is an error, pushes it immediately (does not return).
* **`STA_RP_ON_ERR(status)`**: Evaluates `status`. If it is an error, pushes and returns it immediately.

### Safety Checks
* **`CHECK_HANDLE_R(handle)`** / **`CHECK_HANDLE_RP(handle)`** / **`CHECK_HANDLE_P(handle)`**: Checks if `handle` is NULL. On failure, logs an error, constructs an `ERR_MISSING_HANDLE` critical report, and performs the specified R/RP/P action.
* **`CHECK_NOT_NULL_R(ptr)`** / **`CHECK_NOT_NULL_RP(ptr)`**: Checks if `ptr` is NULL. On failure, logs an error, constructs an `ERR_INVALID_ARG` critical report, and performs the specified R/RP action.
* **`CHECK_ARG_R(arg, min, max, override_return)`**: Asserts if `arg` is within `[min, max]`. On failure, logs bounds, builds an `ERR_INVALID_ARG` critical report with `override_return` (or raw value) as payload, and returns.
* **`CHECK_ARG_RP(arg, min, max, override_return)`**: Same as `CHECK_ARG_R` but additionally pushes the error before returning.

### ESP-IDF Call Checks
* **`STA_FROM_ESP(esp_err_expr)`**: Evaluates `esp_err_expr`. If it is not `ESP_OK`, returns a critical `ERR_ESP` status containing the raw `esp_err_t` value. Otherwise, returns `STA_OK`.
* **`CHECK_ESP_CALL_R(esp_err_call)`** / **`CHECK_ESP_CALL_P(esp_err_call)`** / **`CHECK_ESP_CALL_RP(esp_err_call)`**: Evaluates an `esp_err_t` expression. If it fails, logs the failure with the system log handler and performs the specified R/P/RP action.

---

## Status Queue & Configuration APIs

Pushed status reports go into an internal FreeRTOS queue that can be monitored, logged, or forwarded to a remote host (e.g., via BLE).

```c
// Assigns target queue and characteristics configuration for error transmission
void status_assign_error_tx(uint16_t char_uuid, uint8_t buffer_id, QueueHandle_t status_queue);

// Set reporting threshold level (e.g. only enqueue reports at or above this level)
void status_set_rep_mode(uint8_t rep_level);

// Temporarily suspends adding status reports to the queue (prevents error loops during recovery)
void status_suspend(void);

// Restores adding status reports to the queue using the previous reporting level
void status_resume(void);
```

---

## Adding New Error Codes & Owners

The status component uses the X-macro pattern for code-generation of maps, string conversions, and enums.

### Step 1: Declare the Map
Declare your module-specific owner or error maps in their corresponding subdirectory files (e.g. under `components/status/codes/sys/` or `components/status/codes/devices_owners.h`).

```c
#define MY_MODULE_OWNER_MAP(X) \
  X(OWNER_MY_MODULE_BASE, 0xD000, "OWNER_MY_MODULE_BASE") \
  X(OWNER_MY_SUBMODULE,   0xD001, "OWNER_MY_SUBMODULE")
```

### Step 2: Register in status_codes.h
Link your custom map in `components/status/codes/status_codes.h` by adding it to the parent map wrapper:

```c
#define GLOBAL_OWNER_MAP(X) \
    SYS_DEVICE_OWNER_MAP(X) \
    MY_MODULE_OWNER_MAP(X)  \
    ...
```
This automatically updates both the enum declarations and the conversion strings returned by `status_owner_to_name()` / `status_error_to_name()`.
