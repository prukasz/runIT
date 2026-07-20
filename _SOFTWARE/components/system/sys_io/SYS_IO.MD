# System Input/Output Manager (sys_io)

IMPORTANT: If you modify the source files in this component (sys_io.h or sys_io.c), you must update this documentation to reflect the changes.

## Overview
The sys_io component provides a unified, hardware-agnostic API for managing and interacting with general-purpose and specialized input/output pins. It supports digital input/output, analog-to-digital conversion (ADC), digital-to-analog conversion (DAC), pulse-width modulation (PWM), and configurable interrupts. It features a built-in safety protection model to lock pins and prevent conflicts between system submodules.

### Directory Map
```
components/system/sys_io/
├── CMakeLists.txt
├── SYS_IO.MD           # This documentation
├── sys_io.c            # Dispatch pipeline and pin protection validation
└── include/
    └── sys_io.h        # Vtable contract, enums, macros, and API declarations
```

---

## Architecture & Concept

Drivers register themselves to the I/O Manager under a unique `device_id` by implementing the `sys_io_vtable_t` function table. Applications and system submodules interact with I/O functionalities strictly via the manager APIs, which check safety boundaries and delegate down to the registered device driver handle.

### Scoped Pin Protection (Locking)
* A driver vtable contains a 64-bit `protected_pins` mask.
* If a bit is set in `protected_pins`, calls targeting that pin will return `ERR_SYS_IO_PIN_IN_OTHER_USE` immediately.
* Temporary access to a protected pin (e.g. for emergency overrides) can be obtained via scoped unlocking macros.

---

## Core Data Structures & Enums

### I/O Operational Modes
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

### Interrupt Modes
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

### Interrupt Event & Callback
```c
typedef struct {
  uint8_t device_id;
  sys_io_pin_num_t pin_num;
  sys_io_intr_mode_e triggered_by;
  void* user_arg;
} sys_io_intr_event_t;

typedef void (*sys_io_isr_callback_t)(const sys_io_intr_event_t* event);
```

### Interrupt Configuration Structs
```c
typedef struct {
  uint16_t adc_threshold_up_mV;
  uint16_t adc_threshold_down_mV;
  uint16_t adc_threshold_hysteresis_mV;
  uint16_t adc_event_counter_threshold;
} sys_io_adc_int_config_t;

typedef struct sys_io_intr_config_t {
  sys_io_intr_mode_e mode;
  sys_io_isr_callback_t callback;
  void* user_ctx;
  union {
    sys_io_adc_int_config_t adc;
  };
} sys_io_intr_config_t;
```

### Driver Interface Contract (Vtable)
Drivers must bind their physical functions to this table during installation.
```c
typedef struct sys_io_vtable_t {
  status_rep_t (*io_reset)(void* handle, sys_io_pin_num_t pin);
  status_rep_t (*io_set_mode)(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode);
  status_rep_t (*io_configure_intr)(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config);

  status_rep_t (*io_set_level)(void* handle, sys_io_pin_num_t pin, bool level);
  status_rep_t (*io_get_level)(void* handle, sys_io_pin_num_t pin, bool* level);
  status_rep_t (*io_toggle)(void* handle, sys_io_pin_num_t pin);

  status_rep_t (*io_get_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t* out_mV);
  status_rep_t (*io_set_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t voltage_mV);

  status_rep_t (*io_set_pwm_frequency)(void* handle, sys_io_pin_num_t pin, uint32_t frequency_HZ);
  status_rep_t (*io_set_pwm_duty)(void* handle, sys_io_pin_num_t pin, uint32_t duty);

  uint64_t protected_pins; // 64-bit mask of protected pins (0 to 63)
} sys_io_vtable_t;
```

---

## Utility Macros

### Pin Configuration Helpers
* **`SYS_GPIO_NONE`**: Value defining an unmapped or unused GPIO pin (`0xFF`).
* **`IF_PIN(pin_num)`**: Checks if a pin is mapped. Evaluates as `if (pin_num != 0xFF)`.
* **`SYS_IO_HIGH(device_id, pin_num)`** / **`SYS_IO_LOW(device_id, pin_num)`**: Direct setters for digital outputs.
* **`SYS_IO_TOGGLE(device_id, pin_num)`**: Inverts the logical state of the specified output pin.

### Status & Check Wrappers
* **`SYS_IO_STA_W / C / I(code, owner, info)`**: Constructs a warning, critical, or info report with `STATUS_PAYLOAD_SYS_IO` preset.
* **`SYS_IO_CHECK_NOT_NULL_R / RP(ptr)`**: Checks if a pointer is NULL, returning `ERR_INVALID_ARG` on failure.
* **`SYS_IO_CHECK_HANDLE_R / RP(handle)`**: Checks if a handle is NULL, returning `ERR_MISSING_HANDLE` on failure.
* **`VERIFY_PIN_R(pin, pinmask)`**: Validates if `pin < 64` and is enabled inside the device's physical `pinmask`. On failure, returns `ERR_SYS_IO_PIN_DOES_NOT_EXIST`.

### Data Packing & Unpacking
* **`SYS_IO_MAKE_INFO(dev_id, pin_num, extra)`**: Packs a 16-bit device ID, 8-bit pin number, and 32-bit auxiliary debug value into a single 64-bit status payload.
* **`SYS_IO_UNPACK_DEV_ID(info)`**: Extracts the 16-bit device ID from a status payload.
* **`SYS_IO_UNPACK_PIN(info)`**: Extracts the 8-bit pin number from a status payload.
* **`SYS_IO_UNPACK_EXTRA(info)`**: Extracts the 32-bit auxiliary debug value from a status payload.

### Safety Lock Scopes
* **`SYS_IO_LOCK_PIN(dev_id, pin)`**: Adds a pin to the driver's `protected_pins` bitmask.
* **`SYS_IO_UNLOCK_PIN(dev_id, pin)`**: Removes a pin from the driver's `protected_pins` bitmask.
* **`WITH_PIN_UNLOCKED(dev_id, pin)`**: Scoped RAII-like block. Temporarily unlocks the pin, executes the nested block, and guarantees re-locking the pin upon exiting the scope:
  ```c
  WITH_PIN_UNLOCKED(dev_id, pin_num) {
      // Pin is temporarily unlocked here
      sys_io_set_level(dev_id, pin_num, true);
  } // Pin is automatically locked again here
  ```

---

## Manager API Functions

### Digital & Control Actions
* **`sys_io_reset(uint8_t device_id, sys_io_pin_num_t pin)`**: Resets the pin to default configurations.
* **`sys_io_set_mode(uint8_t device_id, sys_io_pin_num_t pin, sys_io_mode_e mode)`**: Sets pin mode (e.g. digital input, output, ADC, DAC, or PWM).
* **`sys_io_configure_intr(uint8_t device_id, sys_io_pin_num_t pin, const sys_io_intr_config_t* config)`**: Configures pin interrupts (rising edge, falling edge, both, or ADC threshold windows).
* **`sys_io_set_level(uint8_t device_id, sys_io_pin_num_t pin, bool level)`** / **`sys_io_get_level(uint8_t device_id, sys_io_pin_num_t pin, bool* level)`**: Reads or writes digital pin levels.
* **`sys_io_toggle(uint8_t device_id, sys_io_pin_num_t pin)`**: Inverts digital output level.

### Analog (ADC / DAC) Actions
* **`sys_io_get_voltage(uint8_t device_id, sys_io_pin_num_t pin, uint32_t* out_mV)`**: Reads analog voltage (in mV) from a pin configured in ADC mode.
* **`sys_io_set_voltage(uint8_t device_id, sys_io_pin_num_t pin, uint32_t voltage_mV)`**: Outputs analog voltage (in mV) from a pin configured in DAC mode.

### PWM Control Actions
* **`sys_io_set_pwm_frequency(uint8_t device_id, sys_io_pin_num_t pin, uint32_t frequency_HZ)`**: Configures target PWM carrier frequency.
* **`sys_io_set_pwm_duty(uint8_t device_id, sys_io_pin_num_t pin, uint32_t duty)`**: Updates PWM output duty cycle.

### Driver Registration APIs
* **`sys_io_register_driver(uint8_t device_id, void* handle, sys_io_vtable_t* dispatch_table)`**: Registers a device instance to the I/O Manager under a unique ID, linking its function table pointer.
* **`sys_io_unregister_driver(uint8_t device_id)`**: Unregisters the device from the I/O Manager.
