# Design Step 06: Dedicated Error Handling & On-Error Behavior Rules View

## Implemented Component
- File: `app/src/components/settings/ErrorHandlingView.tsx`
- Integrated into: `app/src/App.tsx` and `app/src/components/sidebars/SettingsSidebar.tsx`

## Features & Subsystems

1. **Visibility Tied to Style Mode**:
   - In **Simplified Mode**: The complex GATT tree and the Error Handling rules page are hidden from the sidebar to keep the user experience simple.
   - In **Normal / Dev Mode**: Unlocks the **"Error Handling & Rules"** page in the settings sidebar.

2. **Firmware Error Registry (Left Pane)**:
   - Matches codes from `components/sys_errors/sys_error_handler.c`:
     - `0x01`: `I2C_NACK_EXPANDER` (Bus 1)
     - `0x02`: `PWM_CH_DUTY_OUT_OF_BOUNDS`
     - `0x04`: `WIFI_STA_HANDSHAKE_TIMEOUT`
     - `0x08`: `BLE_GATT_PACKET_OVERFLOW`
     - `0x10`: `BROWN_OUT_DETECTED` (Critical 3.3V rail drop)
   - Displays severity badges (`WARN`, `ERROR`, `CRITICAL`), target subsystem, and last triggered timestamp.

3. **Configurable On-Error Behavior Dispatcher (Right Pane)**:
   Selecting an error allows configuring its exact runtime response:
   - **Ignore & Mute**: Silent discard.
   - **Visual Warning Only**: Non-blocking indicator on the digital twin.
   - **Emergency Safe Stop**: Immediately sets PWM/servo channels to safe duty.
   - **Reset Peripheral Bus**: Sends I2C recovery clock cycles and re-initializes registers.
   - **Software Reboot MCU**: Dispatches `esp_restart()` packet.

4. **Diagnostic Logs Tab**:
   - Secondary tab in top header to switch between "Error Action Rules" and live firmware diagnostic packet logs.

