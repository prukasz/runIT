# Step 15: VM Object JSON Schema Specification

## 1. Overview
Created a single, concise JSON Schema ([vm_object.schema.json](file:///c:/Users/lukasz/Documents/runIT/_SOFTWARE/schema/vm_object.schema.json)) strictly aligned with the C firmware memory layout in `components/VM/core/obj/vm_obj.h` (`vm_obj_head_t` + `payload[]` + tag name) and wire packets in `dec_vm_loader.h`.

## 2. Key Architecture Details

- **Single Schema File**: Located at `schema/vm_object.schema.json` (and mirrored in `app/src/schema/vm_object.schema.json`). Validates both individual `vm_object` items and batch `vm_object_file` collections.
- **Payload Shape**: Supports scalar items (`len: 1`) and 1D tables/buffers (`len > 1`).
- **Relational Tree via PTR**: Pointers (`type: "ptr"`) link to child objects via `refs` referencing by `id` (uint16), by `name` (string <= 15 chars), or both.
- **Telemetry Intent**: `subscribe: boolean` explicitly directs the wire compiler to emit Packet `0x47` (`HEADER_PKT_VM_SUBSCRIBE`).
- **Soft Diagnostic Limits**: `diagnostics.warn_min` / `warn_max` specify UI/canvas warning thresholds without falsely implying firmware-side clamping.
- **Display Helpers**: `unit`, `desc`, `group`, `display.format` (`hex` | `dec` | `bin` | `float`), and `enum_map`.

