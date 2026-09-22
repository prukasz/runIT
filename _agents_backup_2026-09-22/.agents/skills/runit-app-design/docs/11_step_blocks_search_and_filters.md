# Design Step 11: Blocks Palette Search & Category Icon Filters

## Implemented Features in `CodeSidebar.tsx`

1. **Search Bar**:
   - Real-time instant filtering matching block names (e.g. `EXPR`, `TIMER`), descriptions, and C headers (`vm_block_*.h`).
   - Clear input button (`✕`).

2. **Categorized Icon Filter Buttons**:
   - **All**: Displays all available blocks.
   - **Flow** (GitBranch icon, amber): `VM_BLK_IF`, `VM_BLK_SWITCH`, `VM_BLK_FOR`.
   - **Math** (Calculator icon, purple): `VM_BLK_EXPR` (Float32 RPN), `VM_BLK_EXPR_BIT` (UInt32 Bitwise).
   - **Trans / Memory** (Radio icon, blue): `VM_BLK_SET`, `VM_BLK_CLONE` (payload copies & transfer).
   - **I/O & Time** (Zap icon, emerald): `VM_BLK_EDGE`, `VM_BLK_TIMER`, `VM_BLK_IO_SET_LEVEL`, `VM_BLK_IO_TOGGLE`.
   - **Devices** (Cpu icon, cyan): Hardware device blocks e.g. `DEV_BLK_SERVO_PWM` (PCA9685), `DEV_BLK_VREG_CTRL` (TPS55289).

3. **Active Selection Sync**:
   - Clicking any filtered block continues to open its complete C firmware architecture and configuration form on the main screen.

