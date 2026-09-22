# Design Step 04: Settings Side Panel & Global Style Mode

## Implemented Component
- File: `app/src/components/sidebars/SettingsSidebar.tsx`
- Integrated into: `app/src/App.tsx`

## Features & Sections
1. **Board Connectivity**:
   - **BLE (Bluetooth LE)**: Low Energy GATT packets.
   - **WiFi (TCP/UDP Socket)**: High-speed IP telemetry.
   - **Custom Port / Bridge**: Serial, WebSocket, or local pipe.
   - Active mode shows colored indicator and checkmark icon.

2. **Error Handling**:
   - Checkbox toggle for the runtime error interceptor.
   - Status badge displaying `ACTIVE` / `MUTED`.

3. **Style Settings (App Layout Density)**:
   - **Simplified**: Clean presentation hiding raw debug registers.
   - **Normal**: Standard balanced layout.
   - **Dev**: High-density view exposing hex streams, timings, and raw register dumps.
   - This state is stored globally in `App.tsx` and will modulate the visual density of the board, devices, and code canvases.

