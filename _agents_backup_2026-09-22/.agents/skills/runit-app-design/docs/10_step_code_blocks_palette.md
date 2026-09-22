# Design Step 10: Code Tab, Blocks Palette, & Block Inspector

## Implemented Components
1. **Sidebar**: `app/src/components/sidebars/CodeSidebar.tsx`
2. **Main View**: `app/src/components/code/BlockDetailMainView.tsx`

## Features & Navigation

1. **Code Subviews in Sidebar**:
   - **Root Menu**:
     - **Objects & Variables**: Access to the `vm_obj` arena, tags, and accessors.
     - **Blocks Palette (11 Types)**: Direct listing of the real firmware blocks.
   - **Blocks Palette View**:
     - Back navigation button: `← Back to Menu`.
     - 11 real firmware blocks from `components/VM/blocks/vm_blocks.h`:
       - `#1 VM_BLK_EXPR` (RPN Float32 math)
       - `#2 VM_BLK_EXPR_BIT` (RPN UInt32 bitwise logic)
       - `#3 VM_BLK_IF` (Conditional 2-way branch router)
       - `#4 VM_BLK_SWITCH` (N-way switch router)
       - `#5 VM_BLK_FOR` (Loop span owner)
       - `#6 VM_BLK_SET` (Payload memory copy)
       - `#7 VM_BLK_CLONE` (Dynamic object clone)
       - `#8 VM_BLK_EDGE` (Rising/falling edge detector)
       - `#9 VM_BLK_TIMER` (TON / TOF / TP industrial timer)
       - `#10 VM_BLK_IO_SET_LEVEL` (Hardware digital output)
       - `#11 VM_BLK_IO_TOGGLE` (Hardware digital toggle)
     - Categorized with color pills (`MATH`, `FLOW`, `MEMORY`, `TIME`, `IO`).

2. **Block Settings & Description Main View**:
   - **Left Sub-Pane**:
     - Complete C architecture description.
     - Exact C header reference (`components/VM/blocks/vm_block_*.h`).
     - Static function table reference (`g_vm_blocks[]` in `vm_blocks_table.c`).
     - Inputs & Outputs signature (typed accessors).
     - **Dev Mode**: Real packet wire frame preview for `packet_vm_add_block` (`0x45`).
   - **Right Sub-Pane**:
     - **Topological Execution Sequence #** input.
     - Dynamic parameters based on the selected block (e.g. Timer Preset `PT (ms)`, RPN formula editor for `EXPR`, Invert logic for `IF`).
   - **Action**: **`+ Add Block to Canvas`** button in header.

