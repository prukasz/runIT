# System Input/Output Manager (sys_io)

IMPORTANT: If you modify the source files in this component (sys_io.h or sys_io.c), you must update this documentation to reflect the changes.

## Overview
The `sys_io` component provides a unified, hardware-agnostic API for digital I/O, ADC, DAC, PWM, and configurable interrupts across any device installed in the `sys_device` registry. A driver publishes an `sys_io_contract_t` as its `SYS_DEVICE_CONTRACT_IO` contract (see [[SYS_DEVICE.MD]]); `sys_io`'s manager functions look the device up by `device_id`, validate its state and per-pin lock status, then dispatch straight into the vtable. It also owns a scoped pin-locking model that lets one submodule reserve a pin against use by another, and publishes pin-level events to `sys_event` (see [[SYS_EVENT.MD]]).

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
* Each installed device instance carries a 64-bit lock mask, `sys_device_t.io_locked_pins` (bit `n` = pin `n` locked). Locks are per instance, not per device type: the IO vtable itself is `static const` and holds no state.
* The owning adapter drives its own locked pin with `sys_io_set_locked_level(ref, level)`: it unlocks only if the pin was actually locked, sets the level, and restores the previous lock state on every path, including failure.

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
  sys_io_intr_mode_e mode; // Interrupt edge or threshold mode
  bool debounce;           // filter switch bounce (ESP GPIO); off for chip alert lines
  union {
    sys_io_adc_int_config_t adc; // ADC window threshold parameters
  };
} sys_io_intr_config_t;
```
The config only arms the pin. What happens on a trigger is decided by the
listeners of `(SYS_EVENT_DOMAIN_IO, device, pin)`, see [[SYS_EVENT.MD]].

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
* **`SYS_IO_REF(dev_id, pin_num)`**: Compound-literal form for an operation that does not use a mode.
* **`SYS_IO_PIN_INIT(dev_id, pin_num, pin_mode)`** / **`SYS_IO_PIN_NONE_INIT`**: Brace-only forms, for file-scope/static initializers where a compound literal isn't a constant expression.

### Driver Interface Contract (sys_io_contract_t)
Drivers bind their physical functions to this table and publish it as their `SYS_DEVICE_CONTRACT_IO` contract.
```c
typedef struct sys_io_contract_t {
  err_h (*reset)(void* handle, sys_io_pin_num_t pin);
  err_h (*set_mode)(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode);
  err_h (*configure_intr)(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config);

  err_h (*set_level)(void* handle, sys_io_pin_num_t pin, bool level);
  err_h (*get_level)(void* handle, sys_io_pin_num_t pin, bool* level);
  err_h (*toggle)(void* handle, sys_io_pin_num_t pin);

  err_h (*get_voltage)(void* handle, sys_io_pin_num_t pin, int32_t* out_mV);  /* signed: a negative reading is reported, not wrapped */
  err_h (*set_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t voltage_mV);

  err_h (*set_pwm_frequency)(void* handle, sys_io_pin_num_t pin, uint32_t frequency_Hz);
  err_h (*set_pwm_duty)(void* handle, sys_io_pin_num_t pin, uint32_t duty);

} sys_io_contract_t;
```
Any function left NULL causes `SYS_IO_DISPATCH` to fall through to `ERR_DEV_FEATURE_UNAVAILABLE` (`dev_id`, `SYS_DEVICE_CONTRACT_IO`, and a `feature_id` naming which vtable slot was missing) for that pin/device — not the generic `ERR_BASE_NOT_SUPPORTED`, so the trace says *which* operation is unsupported.

### Feature IDs
A feature id is the member's index in `sys_io_contract_t` (`SYS_DEV_FEATURE_ID`, see [[SYS_DEVICE.MD]]); there is no separate enum. `sys_io_feature_names[]` lists the member names in order (NULL-terminated, `_Static_assert`-checked against the struct). `ERR_DEV_FEATURE_UNAVAILABLE`'s logged description uses it to name `feature_id`.

---

## Utility & Type-Safe Operations

### Pin Validation
* **`SYS_GPIO_NONE`**: Value defining an unmapped or unused GPIO pin (`0xFF`).
* **`sys_io_pin_is_valid(ref)`**: Inline function returning `true` if `ref.pin != SYS_GPIO_NONE`.
* **`VERIFY_PIN(dev_id, pin, pinmask)`**: Returns `ERR_IO_PIN_UNAVAILABLE` if `pin >= 64` or the corresponding bit is unset in `pinmask` — the standard first check inside a driver's own functions, ahead of the manager's own dispatch checks.

### Pin Events
* **`sys_io_publish(device_id, pin, event, value, hops)`**: Called by IO adapters when an armed pin triggers (task or ISR). `event` is the edge (`SYS_IO_INTR_MODE_RISING_EDGE` / `_FALLING_EDGE`) for a digital pin or the armed window mode for an analog one; `value` is the level or mV. `hops` is `SYS_EVENT_CAUSED_BY(cause)` inside a listener (an expander publishing its pins from its alert-pin listener), else 0.
* **`sys_io_subscribe_pin(ref, handler, ctx, &id)`**: Chains a device to an alert pin: an inline, system-owned subscription to every event of that pin. The adapter keeps `id` and calls `sys_event_unsubscribe(id, false)` on uninstall. Chip alert pins are configured without debounce.

### Pin Locking & Safety Scopes
* **`sys_io_lock_pin(ref)`**: Sets the referenced pin's bit in the device's `io_locked_pins`.
* **`sys_io_unlock_pin(ref)`**: Clears the referenced pin's bit in the device's `io_locked_pins`.
* **`sys_io_set_locked_level(ref, level)`**: Sets the level of a locked pin. Unlocks only if it was locked and always restores the lock, so the result can be returned directly:
  ```c
  SYS_DEV_TRY(sys_io_set_locked_level(ctx->cfg.oe_pin, true), ctx);
  ```

---

## Manager API Functions

All return `err_h` (`NULL` on success). Every dispatched call routes through `SYS_IO_DISPATCH`, which returns `ERR_DEV_NOT_FOUND`, `ERR_DEV_NOT_INSTALLED`, `ERR_DEV_SUSPENDED`, or `ERR_IO_PIN_LOCKED` before ever reaching the driver, and `ERR_DEV_FEATURE_UNAVAILABLE` (with the calling function's `sys_io_feature_e`) if the vtable slot is NULL.

### Digital & Control Actions
* **`sys_io_reset(ref)`**: Resets the referenced pin to its default configuration.
* **`sys_io_set_mode(ref)`**: Sets the referenced pin to `ref.mode`.
* **`sys_io_configure_intr(ref, config)`**: Configures pin interrupts and callback route mask.
* **`sys_io_set_level(ref, level)`** / **`sys_io_get_level(ref, out_level)`**: Writes or reads a digital pin level.
* **`sys_io_toggle(ref)`**: Inverts digital output level.

### Analog (ADC / DAC) Actions
* **`sys_io_get_voltage(ref, out_mV)`**: Reads analog voltage (mV).
* **`sys_io_set_voltage(ref, voltage_mV)`**: Outputs analog voltage (mV).

### PWM Control Actions
* **`sys_io_set_pwm_frequency(ref, frequency_Hz)`**: Configures target PWM frequency.
* **`sys_io_set_pwm_duty(ref, duty)`**: Updates PWM output duty cycle.

---

## Error Ownership

Every function in `sys_io.c` sets `#undef OWNER` / `#define OWNER OWNER_SYS_IO_<FUNCTION>` immediately before its body, per the owner tagging convention (runit-esp skill, errors.md). See `sys_error_io.h` for the full `SYS_IO_OWNER_MAP` and the `SYS_ERROR_IO_MAP` tags (`ERR_IO_PIN_UNCONFIGURED`, `ERR_IO_PIN_UNAVAILABLE`, `ERR_IO_PIN_ALREADY_IN_USE`, `ERR_IO_PIN_FEATURE_UNSUPPORTED`, `ERR_IO_PIN_LOCKED`, `ERR_IO_PIN_MODE_UNSUPPORTED`).
