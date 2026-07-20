# System I2C Manager (sys_i2c)

IMPORTANT: If you modify the source files in this component (sys_i2c.h, sys_i2c.c, or sys_i2c_types.h), you must update this documentation to reflect the changes.

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
    └── sys_i2c_types.h    # Structure contracts, type definitions, and callbacks
```

---

## Core Data Structures

### I2C Driver Header (sys_i2c_driver_header_t)
This driver header is embedded at the beginning of all device adapter context structures (e.g. TPS55289, PCA9685) to allow unified polymorphism when invoking I2C operations.

```c
typedef struct sys_i2c_driver_header_t {
    i2c_master_dev_handle_t i2c_master_dev_handle; // Bound ESP-IDF master device handle
    i2c_device_config_t     i2c_device_config;     // Target device configuration (address, speed)
    i2c_port_t              bus_num;               // Physical bus index (0 or 1)
        
    // Abstract transmission callback pointer (points to sys_i2c_master_transmit)
    esp_err_t (*transmit)(void* ctx, 
                        const uint8_t *write_buffer, 
                        size_t write_size);
                        
    // Abstract write-read callback pointer (points to sys_i2c_master_transmit_receive)
    esp_err_t (*transmit_receive)(void* ctx, 
                                const uint8_t *write_buffer, 
                                size_t write_size, 
                                uint8_t *read_buffer, 
                                size_t read_size);
} sys_i2c_driver_header_t;
```

---

## I2C Bus Definitions
The system predefines indices for target buses:
* **`SYS_I2C_BUS0`**: Typically maps to internal configuration buses.
* **`SYS_I2C_BUS1`**: Typically maps to external accessory buses.

---

## API Functions

### Bus Lifecycle & Query APIs
* **`sys_i2c_init(bus0_config, bus1_config)`**:
  Initializes physical I2C ports (Bus 0 and Bus 1) based on the provided configuration structures and creates their master bus handles.
* **`sys_i2c_get_bus_handle(bus_num)`**:
  Retrieves the underlying ESP-IDF master bus handle for the specified physical port index.

### Device Adapter Lifecycle APIs
* **`sys_i2c_add_driver(device_handle)`**:
  Binds a new device context to its physical master bus. Automatically binds the abstract `transmit` and `transmit_receive` function pointers in the device's driver header.
* **`sys_i2c_remove_driver(device_handle)`**:
  Removes a previously configured driver context and unbinds its master device handle to free bus resource slots.
* **`sys_i2c_device_present(device_handle)`**:
  Probes the physical bus to verify if a device is active and acknowledging communication at the target address. Returns `ERR_I2C_DEV_NOT_FOUND` on failure.

### Low-Level Transaction APIs
* **`sys_i2c_master_transmit(device_handle, write_buffer, write_size)`**:
  Transmits raw data packets to the target device. Retries automatically on transient timeout or invalid response states before reporting failure.
* **`sys_i2c_master_transmit_receive(device_handle, write_buffer, write_size, read_buffer, read_size)`**:
  Executes a write-then-read sequence (restart transaction). Includes automatic retries for transient failures.
