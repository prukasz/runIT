# System Architecture & Submodules (system)

IMPORTANT: If you add, modify, or delete system submodules, you must update this master documentation to reflect the structural changes.

## Overview
The `system` component serves as the central hardware abstraction layer (HAL) and operating infrastructure for the runIT firmware. It translates generic system interface requests (e.g. "turn on voltage regulator", "toggle pin 10", "transmit packet") into driver-specific actions while maintaining unified state machines, thread safety, and safety limits.

### Directory Map
```
components/system/
├── SYSTEM.MD             # This master architecture documentation
├── ble/                  # BLE Manager & NimBLE host abstraction
│   └── SYS_BLE.MD        # BLE manager documentation
├── callbacks/            # Dynamic event callback routing & execution queue
│   └── SYS_CALLBACKS.MD  # Callback system documentation
├── error_handler/        # System-wide diagnostics & event handlers
├── sys_buffers/          # Shared FreeRTOS ring buffer payload formatter
│   └── SYS_BUFFERS.MD    # Buffer manager documentation
├── sys_device/           # Polymorphic central device registry
│   └── SYS_DEVICE.MD     # Device manager documentation
├── sys_i2c/              # Physical I2C master bus routing & transactions
│   └── SYS_I2C.MD        # I2C manager documentation
├── sys_interface/        # Command binding and external communications
├── sys_io/               # Pin configuration, interrupts, and ADC inputs
│   └── SYS_IO.MD         # I/O manager documentation
└── sys_power/            # Telemetry, regulators, USB-PD, and safety budget
    └── SYS_POWER.MD      # Power manager documentation
```

---

## Architecture & Data Flow

The diagram below illustrates how submodules in the `system` component isolate high-level application code from low-level physical device drivers and ESP-IDF stack.

```mermaid
graph TD
    %% Layers
    subgraph APP [Application Layer]
        runit[runIT Application]
    end

    subgraph SYS [System Manager Layer]
        sys_dev[sys_device<br/>Device Registry]
        sys_pwr[sys_power<br/>Power Budget]
        sys_io[sys_io<br/>GPIO / Interrupts]
        sys_ble[sys_ble<br/>TX/RX Queues]
        sys_i2c[sys_i2c<br/>Master Bus Mgr]
        sys_cb[callbacks<br/>Event Queue Task]
        sys_buf[sys_buffers<br/>Ringbuffer Format]
    end

    subgraph DRV [Driver Adapter Layer]
        adapter_drv8962[device_drv8962]
        adapter_pca9685[device_pca9685]
        adapter_gpio[device_gpio_esp]
    end

    subgraph HAL [ESP-IDF Hardware Layer]
        idf_gpio[GPIO / ADC HAL]
        idf_i2c[I2C Master Driver]
        idf_ble[NimBLE Host Stack]
    end

    %% Flow connections
    runit -->|Query / Control| sys_pwr
    runit -->|IO Actions| sys_io
    runit -->|Send telemetry| sys_ble

    sys_pwr -->|Feature Contract| sys_dev
    sys_io -->|Feature Contract| sys_dev

    sys_dev -.->|Dispatches Vtable| adapter_drv8962
    sys_dev -.->|Dispatches Vtable| adapter_pca9685
    sys_dev -.->|Dispatches Vtable| adapter_gpio

    adapter_drv8962 -->|I2C Transactions| sys_i2c
    adapter_pca9685 -->|I2C Transactions| sys_i2c

    sys_i2c -->|Probe / Transmit| idf_i2c
    sys_io -->|Pin operations| idf_gpio
    sys_io -->|Trigger ISR events| sys_cb
    sys_pwr -->|Trigger telemetry alerts| sys_cb
    sys_ble -->|Async Task| idf_ble
    sys_ble -->|Format Packets| sys_buf
```

---

## Submodule Overview

### 1. Central Device Manager ([sys_device](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_device/SYS_DEVICE.MD))
Acts as the central registry for all hardware modules present on the board.
* **Polymorphism**: Binds context handles and pointers to specific feature contracts (`contracts` vtables).
* **Contracts Supported**:
  * `0`: I/O (`sys_io_vtable_t`)
  * `1`: VREG (Voltage Regulator)
  * `2`: Power Monitor (Telemetry)
  * `3`: USB-PD (USB Power Delivery)

### 2. I/O Manager ([sys_io](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_io/SYS_IO.MD))
Coordinates physical and expanded pin states.
* **Abstract Addressing**: Translates high-level device ID and pin index offsets (e.g. ESP GPIO vs. expander pins) dynamically.
* **Interrupt & Safety Locking**: Handles ISR configurations (`SYS_IO_CB`) and safety lock scopes (`WITH_PIN_UNLOCKED`).

### 3. Power Manager ([sys_power](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_power/SYS_POWER.MD))
Implements safety boundaries and telemetry collection.
* **Power Budget Allocation**: Protects circuits against brownouts by tracking total regulator power budget (`SYS_POWER_BUDGET_MW`).
* **Safety Thresholds**: Lockable write-once configuration thresholds set at startup (`SYS_POWER_LIMIT_MV`, `SYS_POWER_LIMIT_MA`).

### 4. I2C Bus Manager ([sys_i2c](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_i2c/SYS_I2C.MD))
Coordinates shared communication buses (`SYS_I2C_BUS0` for internal circuits, `SYS_I2C_BUS1` for external ports).
* **Fault Tolerance**: Automatic retries on timeout or response errors during read/write restart sequences.
* **Polymorphic Device Registry**: Automatically sets transmission callbacks (`transmit` and `transmit_receive`) on target drivers via `sys_i2c_driver_header_t`.

### 5. BLE Asynchronous Manager ([sys_ble](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/ble/SYS_BLE.MD))
Encapsulates NimBLE configurations.
* **Non-Blocking Sends**: Queues outgoing indications/notifications into priority TX slots (`sys_tx_buff_t`).
* **Event Notifications**: Dequeues incoming GATT writes into RX ring buffers, waking client worker threads via semaphores.

### 6. Dynamic Callback Manager ([callbacks](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/callbacks/SYS_CALLBACKS.MD))
Provides asynchronous ISR and thread-safe event queue processing.
* Enqueues `io`, `pwr`, `ble`, or `own_func` events into a FreeRTOS static queue.
* Dispatches queued events asynchronously inside `sys_cb_task`.

### 7. Transmit Buffer Manager ([sys_buffers](file:///c:/Users/krolp/Documents/runIT_main/_SOFTWARE/components/system/sys_buffers/SYS_BUFFERS.MD))
Formats FreeRTOS ring buffer packets into contiguous byte arrays.
* Prepends header ID bytes to output buffer array frames.
* Handles both `RINGBUF_TYPE_NOSPLIT` items and `RINGBUF_TYPE_BYTEBUF` payload packing.

### 8. Error & Status Logger (`error_handler/`)
Central endpoint for catching warnings and critical diagnostics (ignored for documentation for now).

### 9. System Interface (`sys_interface/`)
Decoder dispatcher for incoming packets (ignored for documentation for now).
