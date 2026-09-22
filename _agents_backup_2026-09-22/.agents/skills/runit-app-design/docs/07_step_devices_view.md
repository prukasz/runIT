# Design Step 07: Devices Tab, C Configuration, & Command Overlay

## Implemented Components
1. **Sidebar**: `app/src/components/sidebars/DevicesSidebar.tsx`
2. **Main View**: `app/src/components/devices/DevicesMainView.tsx`
3. **Independent Modal**: `app/src/components/devices/CommandModal.tsx`

## Features & Subsystems

1. **Simplified Mode Filtering**:
   - In **Simplified Mode**: The onboard hardware ICs (`PCA9685`, `TPS55289`, `INA3221`, `AP33772S`, `ADS7128`) are hidden to present a clean, non-technical view. Only user-attached custom devices are visible.
   - In **Normal / Dev Mode**: Complete hardware IC inventory is displayed under "Onboard ICs" with their respective I2C bus and addresses.

2. **Device Creation Actions**:
   - **`+ Add Device`**: Quickly instantiates a new device entry.
   - **`Add Device Profile (JSON)`**: Allows importing device definitions directly from a JSON schema.

3. **C-Firmware Struct Configuration (Left Pane)**:
   - Configures the real C struct fields from `d_pca9685_cfg_t`, `d_tps55289_cfg_t`:
     - `i2c_bus` (Bus 0 / Bus 1)
     - `i2c_addr` (7-bit hex)
     - `oe_pin` (Output Enable pin ref or `0xFF` / `SYS_IO_PIN_NONE`)
     - `pwm_frequency`
   - In **Dev Mode**: Shows live wire frame hex preview matching Class `0x01` (`dec_sys_device_install.h`).

4. **Action Sequence Triggers**:
   - **`Add to Start Actions`**: Hooks device initialization into boot actions (`SYS_ACTIONS_CLASS_HEADER 0x03`).
   - **`Add to Custom Action`**: Bundles configuration into a dynamic callable action macro slot.

5. **Hardware Command Execution & Independent Overlay Modal**:
   - Grid of real `sys_io` / `sys_device` operations:
     - `sys_io_set_level` (Packet `0x22`)
     - `sys_io_toggle` (Packet `0x24`)
     - `sys_io_set_pwm_duty` (Packet `0x28`)
     - `sys_io_set_pwm_frequency` (Packet `0x27`)
     - `sys_device_reset` (Packet `0x11`)
   - Clicking **Execute** opens an independent modal overlay with live parameter inputs (pin selection, logic level switches, duty sliders) and a "Send Packet" dispatcher.

