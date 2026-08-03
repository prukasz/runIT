# System Input/Output Manager (sys_io)

IMPORTANT: If you modify the source files in this component (sys_io.h or sys_io.c), you must update this documentation to reflect the changes.

## Overview
The `sys_io` component provides a unified, hardware-agnostic API for digital I/O, ADC, DAC, PWM, and configurable interrupts across any device installed in the `sys_device` registry. A driver publishes an `sys_io_vtable_t` as its `SYS_DEVICE_CONTRACT_IO` contract (see [[SYS_DEVICE.MD]]); `sys_io`'s manager functions look the device up by `device_id`, validate its state and per-pin lock status, then dispatch straight into the vtable. It also owns a scoped pin-locking model that lets one submodule reserve a pin against use by another, and integrates pin-level events into the central `callbacks` system.

### Directory Map
```
components/system/sys_io/
├── CMakeLists.txt
├── SYS_IO.MD           # This documentation
├── sys_io.c            # Dispatch pipeline (SYS_IO_DISPATCH) and mode/intr name tables
└── include/
    ├── sys_io.h        # Vtable contract, pin_ref_t, macros, and API declarations
    └── sys_error_io.h  # sys_io owner map and ERR_IO_* error tag map
```

---

## Architecture & Concept

Unlike `sys_device`, `sys_io` keeps no registry of its own: `device_handle` lives on the `sys_device_t` instance, and the vtable is read straight off the class (`dev->cls->contracts[SYS_DEVICE_CONTRACT_IO]`) — set once at class-definition time on the device's `sys_device_class_t`, no per-instance copy involved.

Every manager call (`SYS_IO_DISPATCH` in sys_io.c) goes through the same pipeline: resolve the device, check it's installed and not suspended, check the target pin isn't locked, then call the vtable function and propagate its `err_h` result.

### Scoped Pin Protection (Locking)
* Each vtable instance carries a 64-bit `protected_pins` mask (bit `n` = pin `n` locked).
* If the targeted pin's bit is set, `SYS_IO_DISPATCH` returns `ERR_IO_PIN_LOCKED` before ever reaching the driver.
* Temporary access to a locked pin (e.g. for an emergency override or a dependent device's setup step) is obtained via the scoped `WITH_PIN_UNLOCKED` / `WITH_REF_UNLOCKED` macros, which unlock only if the pin was actually locked and always restore the previous state on scope exit — nesting-safe by construction since each scope remembers whether it did the unlocking.

---

## Core Data Structures & Enums

### I/O Operational Modes (sys_io_mode_e)
```c
typedef enum sys_io_mode_e {
  SYS_IO_MODE_INPUT = 0,
  SYS_IO_MODE_INPUT_PULLUP = 1,
  SYS_IO_MODE_INPUT_PULLDOWN = 2,
  SYS_IO_MODE_OUTPUT_PUSH_PULL = 3,
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN = 4,
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP = 5,
  SYS_IO_MODE_PWM = 6,
  SYS_IO_MODE_ADC = 7,
  SYS_IO_MODE_DAC = 8
} sys_io_mode_e;
```
String form available via `sys_io_mode_e_to_string[]`.

### Interrupt Modes (sys_io_intr_mode_e)
```c
typedef enum sys_io_intr_mode_e {
  SYS_IO_INTR_DISABLE = 0,
  SYS_IO_INTR_MODE_RISING_EDGE = 1,
  SYS_IO_INTR_MODE_FALLING_EDGE = 2,
  SYS_IO_INTR_MODE_BOTH_EDGES = 3,
  SYS_IO_INTR_ADC_WINDOW_OUTSIDE = 4,
  SYS_IO_INTR_ADC_WINDOW_INSIDE = 5,
} sys_io_intr_mode_e;
```
String form available via `sys_io_intr_mode_e_to_string[]`.

### Interrupt Configuration Structs
```c
typedef struct {
  uint16_t adc_threshold_up_mV;
  uint16_t adc_threshold_down_mV;
  uint16_t adc_threshold_hysteresis_mV;
  uint16_t adc_event_counter_threshold;
} sys_io_adc_int_config_t;

typedef struct sys_io_intr_config_t {
  sys_io_intr_mode_e mode;  // Interrupt edge or threshold mode
  uint16_t route_mask;      // Callback event routing mask
  uint64_t action_mask;     // Bitmask of sys_actions ids to invoke (bit i -> action id i); 0 means none
  own_funct_t own_func;     // Optional custom callback function pointer struct
  union {
    sys_io_adc_int_config_t adc; // ADC window threshold parameters
  };
} sys_io_intr_config_t;
```

### Pin Reference (sys_io_pin_ref_t)
Collapses the `(device_id, pin, mode)` triple device configs used to pass as three separate positional args, so callers name what they mean instead of counting.
```c
typedef struct sys_io_pin_ref_t {
  uint8_t device_id;
  sys_io_pin_num_t pin;  /* SYS_GPIO_NONE when unused */
  sys_io_mode_e mode;
} sys_io_pin_ref_t;
```
**Warning**: an unused pin ref must be spelled `SYS_IO_PIN_NONE` / `SYS_IO_PIN_NONE_INIT`. Omitting a pin ref from a designated initializer zero-fills it to `{device 0, pin 0, SYS_IO_MODE_INPUT}` — device 0 and pin 0 are both real, so a silent omission means "drive pin 0", not "unused".
* **`SYS_IO_PIN(dev_id, pin_num, pin_mode)`** / **`SYS_IO_PIN_NONE`**: Compound-literal forms, for automatic storage inside function bodies.
* **`SYS_IO_PIN_INIT(dev_id, pin_num, pin_mode)`** / **`SYS_IO_PIN_NONE_INIT`**: Brace-only forms, for file-scope/static initializers where a compound literal isn't a constant expression.

### Driver Interface Contract (sys_io_vtable_t)
Drivers bind their physical functions to this table and publish it as their `SYS_DEVICE_CONTRACT_IO` contract.
```c
typedef struct sys_io_vtable_t {
  err_h (*io_reset)(void* handle, sys_io_pin_num_t pin);
  err_h (*io_set_mode)(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode);
  err_h (*io_configure_intr)(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config);

  err_h (*io_set_level)(void* handle, sys_io_pin_num_t pin, bool level);
  err_h (*io_get_level)(void* handle, sys_io_pin_num_t pin, bool* level);
  err_h (*io_toggle)(void* handle, sys_io_pin_num_t pin);

  err_h (*io_get_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t* out_mV);
  err_h (*io_set_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t voltage_mV);

  err_h (*io_set_pwm_frequency)(void* handle, sys_io_pin_num_t pin, uint32_t frequency_HZ);
  err_h (*io_set_pwm_duty)(void* handle, sys_io_pin_num_t pin, uint32_t duty);

  uint64_t protected_pins; // 64-bit bitmask of locked pins (0 to 63)
} sys_io_vtable_t;
```
Any function left NULL causes `SYS_IO_DISPATCH` to fall through to `ERR_DEV_FEATURE_UNAVAILABLE` (`dev_id`, `SYS_DEVICE_CONTRACT_IO`, and a `feature_id` naming which vtable slot was missing) for that pin/device — not the generic `ERR_BASE_NOT_SUPPORTED`, so the trace says *which* operation is unsupported.

### Feature IDs (sys_io_feature_e)
One value per `sys_io_vtable_t` slot, in declaration order — carried as `ERR_DEV_FEATURE_UNAVAILABLE`'s `feature_id` payload field:
```c
typedef enum sys_io_feature_e {
  SYS_IO_FEATURE_RESET = 0,
  SYS_IO_FEATURE_SET_MODE,
  SYS_IO_FEATURE_CONFIGURE_INTR,
  SYS_IO_FEATURE_SET_LEVEL,
  SYS_IO_FEATURE_GET_LEVEL,
  SYS_IO_FEATURE_TOGGLE,
  SYS_IO_FEATURE_GET_VOLTAGE,
  SYS_IO_FEATURE_SET_VOLTAGE,
  SYS_IO_FEATURE_SET_PWM_FREQUENCY,
  SYS_IO_FEATURE_SET_PWM_DUTY,
} sys_io_feature_e;
```
String form available via `sys_io_feature_e_to_string[]`. `ERR_DEV_FEATURE_UNAVAILABLE`'s logged description (see [[SYS_ERRORS.MD]]'s `SE_describe_payload()`) uses this table to name `feature_id` when `contract_id == SYS_DEVICE_CONTRACT_IO` — `sys_error_dev.h` (owned by `sys_device`) can't `#include sys_io.h` at that point in the header chain (breaks include ordering), so it forward-declares just this one `extern` array instead of including the whole header.

---

## Utility Macros

### Pin Configuration Helpers
* **`SYS_GPIO_NONE`**: Value defining an unmapped or unused GPIO pin (`0xFF`).
* **`IF_PIN(pin_num)`**: Checks if a pin is mapped (`pin_num != SYS_GPIO_NONE`).
* **`SYS_IO_HIGH(device_id, pin_num)`** / **`SYS_IO_LOW(device_id, pin_num)`**: Direct setters for digital output levels.
* **`SYS_IO_TOGGLE(device_id, pin_num)`**: Inverts output level.
* **`VERIFY_PIN(dev_id, pin, pinmask)`**: Returns `ERR_IO_PIN_UNAVAILABLE` if `pin >= 64` or the corresponding bit is unset in `pinmask` — the standard first check inside a driver's own functions, ahead of the manager's own dispatch checks.

### Pin Reference Operations
Pass an lvalue only — `(ref)` is evaluated more than once.
* **`IF_PIN_REF(ref)`**: `IF_PIN((ref).pin)`.
* **`SYS_IO_REF_SET_MODE(ref)`** / **`SYS_IO_REF_HIGH(ref)`** / **`SYS_IO_REF_LOW(ref)`** / **`SYS_IO_REF_RESET(ref)`**: Manager calls addressed via the ref's `(device_id, pin[, mode])`.
* **`SYS_IO_REF_LOCK(ref)`** / **`SYS_IO_REF_UNLOCK(ref)`** / **`WITH_REF_UNLOCKED(ref)`**: Lock helpers addressed via the ref.

### Event Callback Macro
* **`SYS_IO_CB(_ctx, _pin, _event, _value, _route_mask, _action_mask)`**: Constructs a `cb_event_t` with `CALLBACK_IO` type, populates the `io` payload fields (`device_id` taken from `(_ctx)->base.device_id`) plus `head.route_mask`/`head.action_id`, and triggers `sys_callback_trigger`. `_action_mask` is a bitmask of `sys_actions` ids (bit *i* selects action id *i*) carried through on `cb_event_t.head.action_id` — not yet dispatched to `sys_actions_invoke()` by `sys_cb_task` (see [[SYS_CALLBACKS.MD]]).

### Safety Lock Scopes
Resolve the device's IO vtable via `sys_device_get_by_id()` / `SYS_DEV_GET_CONTRACT()`, so they operate directly on the published contract rather than a local copy.
* **`SYS_IO_LOCK_PIN(dev_id, pin)`**: Sets the pin's bit in the vtable's `protected_pins`.
* **`SYS_IO_UNLOCK_PIN(dev_id, pin)`**: Clears the pin's bit.
* **`WITH_PIN_UNLOCKED(dev_id, pin)`**: Scoped block — unlocks the pin only if it was locked, runs the body, then restores the prior lock state on exit:
  ```c
  WITH_PIN_UNLOCKED(dev_id, pin_num) {
      sys_io_set_level(dev_id, pin_num, true);
  }
  ```

---

## Manager API Functions

All return `err_h` (`NULL` on success). Every dispatched call routes through `SYS_IO_DISPATCH`, which returns `ERR_DEV_NOT_FOUND`, `ERR_DEV_NOT_INSTALLED`, `ERR_DEV_SUSPENDED`, or `ERR_IO_PIN_LOCKED` before ever reaching the driver, and `ERR_DEV_FEATURE_UNAVAILABLE` (with the calling function's `sys_io_feature_e`) if the vtable slot is NULL.

### Digital & Control Actions
* **`sys_io_reset(device_id, pin)`**: Resets the pin to its default configuration.
* **`sys_io_set_mode(device_id, pin, mode)`**: Sets pin mode.
* **`sys_io_configure_intr(device_id, pin, config)`**: Configures pin interrupts and callback route mask.
* **`sys_io_set_level(device_id, pin, level)`** / **`sys_io_get_level(device_id, pin, out_level)`**: Writes or reads a digital pin level.
* **`sys_io_toggle(device_id, pin)`**: Inverts digital output level.

### Analog (ADC / DAC) Actions
* **`sys_io_get_voltage(device_id, pin, out_mV)`**: Reads analog voltage (mV).
* **`sys_io_set_voltage(device_id, pin, voltage_mV)`**: Outputs analog voltage (mV).

### PWM Control Actions
* **`sys_io_set_pwm_frequency(device_id, pin, frequency_HZ)`**: Configures target PWM frequency.
* **`sys_io_set_pwm_duty(device_id, pin, duty)`**: Updates PWM output duty cycle.

---

## Error Ownership

Every function in `sys_io.c` sets `#undef OWNER` / `#define OWNER OWNER_SYS_IO_<FUNCTION>` immediately before its body, per the [[runit]] skill's module owner tagging convention. See `sys_error_io.h` for the full `SYS_IO_OWNER_MAP` and the `SYS_ERROR_IO_MAP` tags (`ERR_IO_PIN_UNCONFIGURED`, `ERR_IO_PIN_UNAVAILABLE`, `ERR_IO_PIN_ALREADY_IN_USE`, `ERR_IO_PIN_FEATURE_UNSUPPORTED`, `ERR_IO_PIN_LOCKED`, `ERR_IO_PIN_MODE_UNSUPPORTED`).
