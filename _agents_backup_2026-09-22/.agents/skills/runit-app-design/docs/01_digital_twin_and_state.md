# Element 01: Digital Twin & Object Tree Hierarchy

## 1. Concept Overview
The Digital Twin acts as the authoritative client-side replica of the runIT board's internal runtime state. It maintains a hierarchical object tree reflecting all hardware peripherals, virtual blocks, variables, accessors, and connection links.

## 2. Key Responsibilities
- **Folder Tree Hierarchy**: Renders a filesystem-like tree view of components (e.g. `runIT_Main` -> `PCA9685_Expander` -> `Servo_Pan`).
- **Accessor & Variable Binding**: Every hardware register, configuration parameter, and runtime value has a unique path/ID (e.g., `devices.pca9685[0].ch[0].pulse_us`).
- **Two-Way Synchronization**:
  - Inbound telemetry packets update the local digital twin state tree.
  - User adjustments (via sliders, controllers, or blocks) mutate the twin and dispatch command packets to the hardware.
- **Project JSON Save/Restore**: Full serialization of the tree, active blocks, wire routes, and initial configurations into a portable JSON schema.
- **Automatic Object Selection**: Context-aware helper that suggests appropriate block or accessor types when creating new objects in the tree.

## 3. UI / Layout Requirements
- Left-side docking panel with collapsible directories.
- Status badges per object (e.g. `OK`, `UNCONFIGURED`, `OFFLINE`, `FAULT`).
- Quick search / filter input for large object trees.
- Add / Delete / Duplicate context menu per node.

