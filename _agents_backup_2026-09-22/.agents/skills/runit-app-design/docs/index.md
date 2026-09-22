# runIT Design Specifications & Memory Index

This directory serves as the persistent memory and design blueprint for each subsystem of the runIT application. As we design each element step-by-step, concepts, schemas, packet definitions, and UI requirements are cataloged here.

## Subsystem Specifications

| ID | Component / Element | File | Status | Description |
|---|---|---|---|---|
| **01** | **Digital Twin & Object Tree** | [01_digital_twin_and_state.md](./01_digital_twin_and_state.md) | Ready for details | Hierarchical object tree, variable bindings, client-side mirror, and project JSON serialization. |
| **02** | **Protocol & Error Mapping** | [02_packet_protocol_and_errors.md](./02_packet_protocol_and_errors.md) | Ready for details | BLE GATT / WiFi packet frame structure, decoder mapping, and raw error correlation to UI elements. |
| **03** | **Visual Block Canvas** | [03_block_canvas.md](./03_block_canvas.md) | Ready for details | Node-RED style visual programming, firmware primitive blocks, accessor ports, and link routing. |
| **04** | **Interactive Board SVG** | [04_board_pinout_svg.md](./04_board_pinout_svg.md) | Ready for details | Hardware pinout viewer, click-to-inspect accessors, peripheral compatibility, and live pin states. |
| **05** | **Remote Controller & Gamepad** | [05_remote_controller.md](./05_remote_controller.md) | Ready for details | Virtual dashboard (sliders, toggles, joysticks), Gamepad API polling, and direct variable injection. |
| **06** | **Telemetry & Action Recorder** | [06_telemetry_and_recorder.md](./06_telemetry_and_recorder.md) | Ready for details | Time-series packet logging, action state recording, and replay engine. |

## Recent Step Implementations
- [10_step_code_blocks_palette.md](./10_step_code_blocks_palette.md) - Blocks palette with 11 C firmware VM blocks.
- [11_step_blocks_search_and_filters.md](./11_step_blocks_search_and_filters.md) - Search bar & category icon filters.
- [12_step_user_objects_tree.md](./12_step_user_objects_tree.md) - User Object Arena & Variable Tree (Python WebUI alignment).
- [13_step_flow_canvas_and_block_config.md](./13_step_flow_canvas_and_block_config.md) - React Flow canvas, drag-and-drop, and block configuration panel.
- [14_step_tabs_navigation_and_header_cleanup.md](./14_step_tabs_navigation_and_header_cleanup.md) - Direct sidebar tabs and top navigation simplification.
- [15_step_vm_object_json_schema.md](./15_step_vm_object_json_schema.md) - Single concise JSON schema for runIT VM objects.



