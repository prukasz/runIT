---
name: runit-app-design
description: Knowledge base, architecture concepts, memory, and design specifications per component for the runIT cross-platform web and mobile application.
---

# runIT App Architecture & Concept Memory

This skill acts as the centralized memory repository for the runIT Studio application design. It documents architectural decisions, component concepts, state models, protocol structures, and step-by-step layout requirements.

## Directory Structure

All design specifications, schemas, and concept descriptions are stored in `docs/`:

- `docs/index.md`: Master index and system roadmap.
- `docs/01_digital_twin_and_state.md`: Digital twin model, object folder tree, accessor/variable mapping, and project JSON serialization.
- `docs/02_packet_protocol_and_errors.md`: BLE/WiFi transport, binary packet encoding/decoding, and runtime error mapping (`sys_errors`).
- `docs/03_block_canvas.md`: Node-RED style block engine, input/math/actuator blocks, and inter-component linking.
- `docs/04_board_pinout_svg.md`: Interactive SVG board visualizer, pin states, and peripheral capabilities.
- `docs/05_remote_controller.md`: Virtual dashboard, sliders, switches, joystick, and Gamepad API integration.
- `docs/06_telemetry_and_recorder.md`: Action/state recorder, ring buffers, and time-series replay.

## Design Workflow Rules

When building or refining any element:
1. Always reference or update the corresponding concept document in `docs/` before implementing code.
2. Maintain single-codebase compatibility (Desktop Web first with seamless Capacitor Android migration).
3. Ensure layout components are responsive, touch-friendly, and match the dark-mode aesthetic.

