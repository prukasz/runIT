# Design Step 08: Hardware Connection & I2C Presence Check

## Implemented Features in `DevicesMainView.tsx`

1. **"Check Connection" Action Button**:
   - Pinned in the top action bar of the device view alongside *Add to Start Actions* and *Add to Custom Action*.
   - Features a search/probe icon and an animated spinner during the round-trip packet ping.

2. **I2C Presence & Handshake Test**:
   - Simulates or dispatches an I2C address probe packet on the configured `i2c_bus` to verify if the physical chip acknowledges with an **ACK**.
   - Handles edge cases (e.g. invalid address `0x00` or `0xFF` returns a simulated **NACK**).

3. **Live Hardware Detection Banner**:
   - Pops up below the header with immediate visual and diagnostic feedback:
     - **Success (Emerald)**: `DEVICE DETECTED & ONLINE` — shows bus number, confirmed address, and RTT latency (e.g. `14ms`).
     - **Failure (Red)**: `DEVICE NOT RESPONDING (NACK)` — provides troubleshooting advice (check pullup resistors, power rail, or wiring).

