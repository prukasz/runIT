# System Device Manager (sys_device)

IMPORTANT: If you modify the source files in this component (sys_device.h or sys_device.c), you must update this documentation to reflect the changes.

## Overview
The Device is the core unit of the runIT system architecture. A device represents any hardware object (control registers, power regulators, sensors, expanders) managed by the system. The `sys_device` component provides a unified management framework, standardized error propagation via `status_rep_t`, and life-cycle operations (install, uninstall, reset, suspend, resume, freeze, sync).

### Directory Map
```
components/system/sys_device/
├── CMakeLists.txt
├── SYS_DEVICE.MD       # This documentation
├── sys_device.c        # Manager implementation and registry storage
└── include/
    ├── sys_device.h    # Core contracts, macros, and API declarations
    └── sys_device_status_codes.h # Status codes and error payload definitions
```

---

## Architecture & Concept

All hardware interaction follows a strict multi-level dispatch pipeline:
```
Hardware Driver (hw) -> Device Adapter (ctx) -> Device Manager (sys_device_t) -> Feature Contracts (sys_io, sys_power, etc.)
```

### Example: Setting a GPIO output level
1. The user calls `sys_io_set_level(device_id, pin, level)`.
2. The system retrieves the device instance via `sys_device_get_by_id(device_id)`.
3. The system checks if the device is installed, not suspended, and supports the I/O feature contract (`SYS_DEVICE_CONTRACT_IO`).
4. The system executes the contract function: `vtable->io_set_level(dev->device_handle, pin, level)`.
5. The device adapter receives the call, extracts its local hardware instance (`hw_handle`), and forwards the command to the driver layer.

---

## Core Data Structures

### sys_device_adapter_base_t
Every custom device adapter context struct must begin with this base struct as its first member, named exactly `base`.
```c
typedef struct {
  void* hw_handle;      // Pointer to the underlying driver-level handle
  uint8_t device_id;    // Unique user-assigned ID for the device instance
  bool is_frozen;       // True if device updates are currently queued/frozen
} sys_device_adapter_base_t;
```

### sys_device_t
The unified device contract instance struct.
```c
typedef struct sys_device_t {
  uint8_t device_id;            // Unique device identifier (0 to 127)
  sys_device_role_e role;       // SYS_DEV_ROLE_DEF, SYS_DEV_ROLE_IO, SYS_DEV_ROLE_PWR, SYS_DEV_ROLE_USER
  void* device_handle;          // Instance-specific adapter context pointer
  const char* name;             // Human-readable device name
  void** install_args;          // Array of installation parameters for re-initialization
  bool is_suspended;            // True if device is currently suspended
  bool is_installed;            // True if device is registered and installed
  struct {                      // Automatic error action configuration flags
    uint8_t suspend_on : 2;
    uint8_t reset_on : 2;
    uint8_t uinstall_on : 2;
    uint8_t emergency_action_on : 2;
  } err_cfg;

  // Lifecycle Callbacks
  status_rep_t (*install_device)(void** install_args, void** out_device_handle);
  status_rep_t (*error_handler)(void* device_handle, status_rep_t* error);
  status_rep_t (*uninstall_device)(void* device_handle);
  status_rep_t (*reset_device)(void* device_handle);
  status_rep_t (*suspend_device)(void* device_handle);
  status_rep_t (*resume_device)(void* device_handle);
  status_rep_t (*freeze_device)(void* device_handle);
  status_rep_t (*sync_device)(void* device_handle);

  // Feature Contracts
  // 0: SYS_DEVICE_CONTRACT_IO (sys_io_vtable_t)
  // 1: SYS_DEVICE_CONTRACT_POWER_VREG (sys_power_vreg_contract)
  // 2: SYS_DEVICE_CONTRACT_POWER_MONITOR (sys_power_monitor_contract)
  // 3: SYS_DEVICE_CONTRACT_POWER_USB_PD (sys_power_usb_pd_contract)
  void* contracts[4];
} sys_device_t;
```

---

## Utility Macros

### Diagnostic & Handle Verification
* **`SYS_DEV_CHECK_HANDLE_R(handle, dev_id)`**: Asserts `handle` is not NULL. Returns `ERR_DEV_MISSING_HANDLE` on failure.
* **`SYS_DEV_CHECK_HANDLE_RP(handle, dev_id)`**: Same as `SYS_DEV_CHECK_HANDLE_R`, but additionally pushes the error report to the system status queue (`STA_P`).
* **`SYS_DEV_STA_W / C / I(code, owner, info)`**: Wraps `STA_W/C/I` to construct warning, critical, or info reports with `STATUS_PAYLOAD_DEV_ESP` preset.

### Driver & Dependency Call Verification
* **`SYS_DEV_CHECK_DRIVER_CALL(driver_call, ctx)`**: Evaluates an `esp_err_t` driver call. If not `ESP_OK`, builds a `status_rep_t` with `STATUS_PAYLOAD_DEV_ESP` payload containing the device ID and ESP error code, and returns it.
* **`SYS_DEV_CHECK_DEP_CALL(status_call, ctx, dep_dev_id)`**: Evaluates a dependent device `status_rep_t` call. If an error is returned, logs the dependency failure and packages a `STATUS_PAYLOAD_DEV_DEP` error.

### Feature Dispatching
* **`IF_SYS_DEV_AND_FEATURE(device_id, contract_type, contract_struct_type, func_member, dev_ptr, vtable_ptr)`**: Evaluates if the device ID exists, is installed, not suspended, supports the specified contract, and implements the function member.
* **`SYS_DEV_DISPATCH(dev_id, contract_enum, contract_type, func_name, ...)`**: Dispatches a function call down to the device contract. If the device is missing, uninstalled, or suspended, returns the appropriate status report (`ERR_DEV_NOT_FOUND`, `ERR_DEV_NOT_INSTALLED`, `ERR_DEV_SUSPENDED`, or `ERR_NOT_SUPPORTED`).

### Parameter Packing / Unpacking
* **`SYS_DEV_ARG_PACK(val)`**: Casts a value to a generic `void*`.
* **`SYS_DEV_ARG_UNPACK(type, var, args, index)`**: Extracts a value of specified type from the `void**` installation arguments array.
* **`SYS_DEV_ARG_UNPACK_VAL(type, args, index)`**: Utility expression that casts an array index directly to the target type.

### Adapter Context Allocation & Double Buffering
* **`SYS_DEV_GET_ADAPTER_CONTEXT(ctx_type, hw_type, ctx_var, hw_var, input_handle)`**: Declares and checks both the adapter context pointer (`ctx_var`) and driver-level hardware handle (`hw_var`) from `input_handle`.
* **`IF_SYS_DEV_FROZEN(ctx)`**: Evaluates if the adapter context has the frozen state enabled (`base.is_frozen`).
* **`SYS_DEV_CTX_FREEZE(ctx)`** / **`SYS_DEV_CTX_UNFREEZE(ctx)`**: Sets `base.is_frozen` state boolean.

---

## Manager API Functions

### Helper & Lifecycle Management
* **`sys_device_allocate_ctx(total_size, install_args)`**: Allocates memory for an adapter context, initializes its base structure, and copies installation arguments.
* **`sys_device_install(sys_device_t* device)`**: Registers a new device entry in the system registry (up to `MAX_DEVICE_ID = 127`) and executes its custom `install_device` function.
* **`sys_device_uninstall(uint8_t device_id)`**: Calls `uninstall_device` to release internal driver references and removes the device from the registry.
* **`sys_device_reset(uint8_t device_id)`**: Invokes the device-specific reset handler.

### Suspend & Resume
* **`sys_device_suspend(uint8_t device_id)`**: Suspends the target device, setting `is_suspended = true`.
* **`sys_device_resume(uint8_t device_id)`**: Restores a suspended device, setting `is_suspended = false`.
* **`sys_device_suspend_all()`** / **`sys_device_resume_all()`**: Broadcasts suspend or resume actions to all registered devices.

### Double Buffering (Freeze & Sync)
* **`sys_device_freeze(uint8_t device_id)`**: Activates freeze mode on the target device context.
* **`sys_device_sync(uint8_t device_id)`**: Pushes cached actions to physical hardware registers and exits freeze mode.
* **`sys_device_freeze_all()`** / **`sys_device_sync_all()`**: Broadcasts freeze or sync commands to all registered devices.

### Registry Query & Error Handling
* **`sys_device_get_by_id(uint8_t device_id)`**: Returns a pointer to the registered `sys_device_t` structure matching the provided ID, or `NULL` if not found.
* **`sys_device_generic_error_handler(dev, err)`**: Default fallback error handler for unhandled device errors, evaluating `err_cfg` actions (suspend, reset, uninstall).
