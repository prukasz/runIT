# Design Step 09: Inter-Device Hardware Relations (sys_io_pin_ref_t)

## Implemented Features in `DevicesMainView.tsx`

1. **Hardware IO Pin Dependencies Box**:
   - Represents the C-firmware `sys_io_pin_ref_t` pattern (`device_id, pin, mode`).
   - Supports:
     - `oe_pin` (Output Enable) for PCA9685 PWM expander.
     - `en_pin` (Regulator Enable) for TPS55289 Buck-Boost.
     - `intr_pin` (Interrupt signal line).

2. **Active vs SYS_IO_PIN_NONE Toggle**:
   - Checkbox to toggle between an active pin reference and `0xFF` (`SYS_IO_PIN_NONE`).

3. **Provider Device Selector & Visual Jump Link**:
   - Dropdown to select which hardware device provides the pin (e.g. `ESP32-S3 Native GPIO` or `TCA6424A 24-bit Expander`).
   - Input for the target pin number (e.g. `Pin 12`).
   - **`Open Device Tab ↗` Quick Action Link**: Clicking this link immediately navigates the workspace to that referenced device's configuration tab.

4. **Dev-Mode Byte Stream Update**:
   - Dev mode wire frame reflects the exact 3-byte flattened pin reference: `[device_id] [pin] [mode]` or `FF FF 00`.

