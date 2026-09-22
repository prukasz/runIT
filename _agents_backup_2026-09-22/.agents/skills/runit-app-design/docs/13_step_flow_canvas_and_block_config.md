# Step 13: Visual Flow Canvas, Drag & Drop, & Block Configuration

## Overview
Implements the visual node flow canvas powered by React Flow (`@xyflow/react`), enabling full drag-and-drop workflow from the Block Palette and Variables Tree, cross-panel synchronization, and an in-depth Block Configuration & Accessor Inspector panel.

## Key Architectures & Features

### 1. Drag & Drop Onto Canvas
- **Block Palette Drag & Drop (`application/runit-block`)**:
  - Dragging any block from the left palette onto the canvas instantiates a `VmBlockNode` at the exact mouse coordinates.
  - Generates unique instance ID and assigns next VM topological execution order.
- **Variable / Folder Drag & Drop (`application/runit-var`)**:
  - Dragging any variable or struct folder from the left sidebar onto the canvas instantiates a `VmAccessorNode` (`vm_accessor_t`, Opcode `0x44`).
  - Wire handles allow connecting accessors directly to block inputs (Read) or block outputs (Write).

### 2. Cross-Panel Context Switching
- **Selecting a Block on Canvas**:
  - **Left Panel**: Automatically switches to the **Variables Tree** (`Objects`), enabling immediate dragging or binding of variables to the newly selected block.
  - **Right Panel (`BlockConfigRightPanel.tsx`)**: Opens the selected block's configuration inspector.

### 3. Right Block Configuration Panel
- **Identity**: Block Name, Type ID, C Header (`vm_block_*.h`), and Execution Order in VM loop.
- **Hardware Device Integration**:
  - Targets physical chips (`PCA9685`, `TPS55289`, etc.) from the device registry.
  - Channel / GPIO Pin selector (e.g. Channel `0..15` for PCA9685 PWM).
- **Limits & Computation Parameters**:
  - Min / Max value bounds clamp.
  - Timer preset interval (ms) for timer blocks.
  - RPN / Math formula expression editor (`f(x) = B - A`).
  - Boolean condition inversion toggle.
- **Accessor Port Bindings (`vm_accessor_t` - Opcode `0x44`)**:
  - Dedicated row for each input port: dropdown selector to bind/unbind any user variable directly without dragging.
  - Quick-jump button: *"View in Variables Tree ➔"*.
  - Output ports representation.
- **Actions**: Delete block from canvas.

### 4. Custom React Flow Nodes
- **`VmBlockNode`**:
  - Category color-coded badge (`FLOW`, `MATH`, `TRANS`, `I/O`, `DEVICES`).
  - Left handles for typed input ports with bound variable tags (`↳ pos_actual`).
  - Right handles for computed outputs.
  - Live output values and formula previews on the node face.
- **`VmAccessorNode`**:
  - Variable tag name, type badge (`F`, `U32`, `B`, `PTR`), object ID, and live value.
  - Left target handle (for Write accessors) and right source handle (for Read accessors).

