# Design Step 05 (Extended): BLE Security & PIN / Passkey Management

## Implemented Features in `BleSettingsView.tsx`

1. **BLE Security & PIN / Passkey Box**:
   - Pinned at the top of the right configuration pane.
   - **Pairing Security Level**:
     - `No Security (Just Works)`
     - `Static 6-Digit PIN / Passkey`
     - `Encrypted MITM (Man-In-The-Middle)`
   - **6-Digit Passkey PIN Input**:
     - Masked with eye toggle (`Show / Hide PIN`).
     - Real-time numerical constraint (only digits).
     - **Save** button with confirmation message (`✓ Passkey updated in flash memory`).
   - Status badge: `BONDING SECURE`.

2. **16-Bit UUID Services & Characteristics Tree**:
   - Retains all previous add/remove and hardcoded vs user-editable capabilities.
   - Individual characteristic editing (Name, UUID, Permissions, Value Format, CCCD, MTU Reassembly).
