# System I2C Manager (sys_i2c)

IMPORTANT: If you modify the source files in this component (sys_i2c.h, sys_i2c.c, sys_i2c_types.h, or sys_i2c_status_codes.h), you must update this documentation to reflect the changes.

## Overview
The `sys_i2c` component provides a unified driver manager for controlling and communicating over physical I2C buses. It manages bus allocation, device registration (adding and removing adapters), address presence checking (probing), and fault-tolerant transmission routing with hardware-level retries.

### Directory Map
```
components/system/sys_i2c/
├── CMakeLists.txt
├── SYS_I2C.MD             # This documentation
├── sys_i2c.c              # Core I2C transaction and bus management logic
└── include/
    ├── sys_i2c.h          # Global configuration macros and lifecycle APIs
    ├── sys_i2c_types.h    # Structure contracts, header definitions, and function declarations
    └── sys_i2c_status_codes.h # Owner map and I2C status code definitions
```

---

## Core Data Structures

### I2C Driver Header (sys_i2c_driver_header_t)
This driver header is embedded at the beginning of all device adapter context structures (e.g. TPS55289, PCA9685) to allow unified polymorphism when invoking I2C operations.

```c
typedef struct sys_i2c_driver_header_t {
    i2c_master_dev_handle_t i2c_master_dev_handle; // Bound ESP-IDF master device handle
    i2c_device_config_t     i2c_device_config;     // Target device configuration (address, clock speed)
    i2c_port_t              bus_num;               // Physical bus index (SYS_I2C_BUS0 or SYS_I2C_BUS1)
        
    // Abstract transmission callback pointer (bound to sys_i2c_master_transmit)
    esp_err_t (*transmit)(void* ctx, 
                        const uint8_t *write_buffer, 
                        size_t write_size);
                        
    // Abstract write-read callback pointer (bound to sys_i2c_master_transmit_receive)
    esp_err_t (*transmit_receive)(void* ctx, 
                                const uint8_t *write_buffer, 
                                size_t write_size, 
                                uint8_t *read_buffer, 
                                size_t read_size);
} sys_i2c_driver_header_t;
```

---

## I2C Bus Definitions & Status Owners

### Physical Bus Indices
* **`SYS_I2C_BUS0`** (`0`): Typically maps to internal system configuration buses.
* **`SYS_I2C_BUS1`** (`1`): Typically maps to external accessory buses.

### Status Owners & Errors (`sys_i2c_status_codes.h`)
* `OWNER_SYS_I2C_BASE` (`0xA100`)
* `OWNER_SYS_I2C_BUS0` (`0xA101`)
* `OWNER_SYS_I2C_BUS1` (`0xA102`)
* `OWNER_SYS_I2C_TRANSMIT` (`0xA103`)
* `OWNER_SYS_I2C_TRANSMIT_RECEIVE` (`0xA104`)
* `OWNER_SYS_I2C_PROBE` (`0xA105`)
* `ERR_I2C_DEV_NOT_FOUND` (`0xA101`): Returned when device probe or transmission fails to receive ACK.

---

## API Functions

### Bus Lifecycle & Query APIs
* **`sys_i2c_init(bus0_config, bus1_config)`**:
  Initializes physical I2C ports (Bus 0 and Bus 1) based on the provided configuration structures and creates their master bus handles.
* **`sys_i2c_get_bus_handle(bus_num)`**:
  Retrieves the underlying ESP-IDF `i2c_master_bus_handle_t` for the specified physical port index.

### Device Adapter Lifecycle APIs
* **`sys_i2c_add_driver(device_handle)`**:
  Binds a new device context to its physical master bus using `i2c_master_bus_add_device`. Automatically populates the abstract `transmit` and `transmit_receive` function pointers in the device's `sys_i2c_driver_header_t`.
* **`sys_i2c_remove_driver(device_handle)`**:
  Removes a previously configured driver context and unbinds its master device handle to free bus resource slots.
* **`sys_i2c_device_present(device_handle)`**:
  Probes the physical bus using `i2c_master_probe` to verify if a device is active and acknowledging communication at the target address. Returns `ERR_I2C_DEV_NOT_FOUND` on failure.

### Low-Level Transaction APIs
* **`sys_i2c_master_transmit(device_handle, write_buffer, write_size)`**:
  Transmits raw data packets to the target device using `i2c_master_transmit`. Retries automatically on transient timeout or response errors before reporting failure.
* **`sys_i2c_master_transmit_receive(device_handle, write_buffer, write_size, read_buffer, read_size)`**:
  Executes a write-then-read sequence (restart transaction) using `i2c_master_transmit_receive`. Includes automatic retries for transient failures.
