# Step 12: User Object Arena & Variable Tree (Python WebUI Alignment)

## Overview
Replicates the visual layout and interaction mechanics of the Python WebUI (`Python/PythonRunIT/web_ui/app.js` and `styles.css`) for user-only variables and structured data hierarchies.

## Architectural Alignment with Firmware (`vm_obj.h`)
- **Header Structure**: `vm_obj_head_t` with 4-bit type, 4-bit name length (enforcing a 15-character maximum tag name), and flag bits (`mutable`, `upd`, `retentive`, `dynamic`, `usr_protected`).
- **VM Loader Packets (Class 0x04)**:
  - `0x42`: Add Objects (Arena allocation)
  - `0x43`: Set Data (Payload values)
  - `0x44`: Add Accessors (Inter-block data piping)
  - `0x47`: Subscribe Telemetry Stream (High-frequency live updates)

## Features & Visual Design
1. **Separate Container Cards (`tree-root-card`)**:
   - Each top-level root tree or standalone scalar root variable has its own distinct card with a dark border and hover highlighting.
   - Header identifies root nodes as `[ROOT TREE #id]` or `[ROOT VAR #id]`.
2. **Two-Row Inline Layout (`tree-node-box`)**:
   - **Row 1 (Main Row)**:
     - Expand/collapse toggle `[-]` / `[+]`.
     - `#id` badge.
     - Type dropdown: `PTR / LINK`, `F`, `U32`, `I32`, `U8`, `B`, `STR` with specific syntax colors.
     - Name input (15 characters max limit with length counter).
     - Matrix detection badge: `[PTR MATRIX RxC]` when children represent uniform row vectors.
     - Subscribe toggle (Eye icon): Cyan when streaming active.
     - Mutable toggle (Pen/Lock icon): Blue pen when writable, amber lock when constant.
     - Pointer alias link selector (`↳ Link: [#id name]`) for redirecting pointers.
     - Quick actions: `+ Field`, `+ Matrix`, `+ Subfolder`, `Del`.
   - **Row 2 (Sub Row)**:
     - Type category label: `struct:`, `matrix (ptr):`, or `data:`.
     - Count / dimension input (`count: N` or `dim: RxC`).
     - Value input with dynamic expansion.
     - Slots indicator badge: shows filled vs available slots (e.g., `1/1 filled`, `6/6 filled`, `12 slots: 3 rows × 4 cols`).
     - Live runtime telemetry pill with pulsing status indicator.
3. **Pointer Alias & Redirected Rows**:
   - Renders linked target variables with dashed amber styling, `↳` bullet, `[REF #id]` badge, redirected value `= val`, and `[Go to def]` jump button.
5. **Split View: Selected Variable & Linked Blocks Reference**:
   - Eliminates wall-of-trees bloat in the main view by focusing on the active selected variable.
   - **Left Split Pane (Inspector & Child Ladder)**:
     - Full two-row inline editor for the selected variable/struct.
     - Breadcrumb navigation trail (`robot_cell / joint_1 / pos_actual [#3]`).
     - Real-time slots fill indicator, telemetry stream pill, and immediate child fields if a container.
   - **Right Split Pane (Referenced in VM Blocks - `vm_accessor_t`)**:
     - Lists all blocks binding to this variable with port names, data directions (`READ` / `WRITE`), and descriptions.
     - `Highlight in Blocks Canvas ➔` button: dynamically transitions the view to the block palette / canvas with that block highlighted, adding the step to the navigation breadcrumb history.


