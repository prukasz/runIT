# Element 03: Visual Block Canvas (Node Flow)

## 1. Concept Overview
A visual node graph editor (similar to Node-RED or LabVIEW) allowing users to visually link hardware accessors, math transformers, and controllers without writing code on the client. Execution runs directly on the board or through synchronized twin variables.

## 2. Key Responsibilities
- **Node-RED Style Canvas**: Powered by React Flow (`@xyflow/react`) with panning, zooming, minimap, and grid snapping.
- **Node Types**:
  - *Source / Input Nodes*: Virtual joystick, Gamepad axis, Timer, Digital input pin, ADC value.
  - *Transform / Logic Nodes*: Range mapper (e.g. `-1..1` -> `1000..2000us`), Inverter, Math clamp, Deadband filter, State latch.
  - *Actuator / Sink Nodes*: PWM pin output, Servo channel, DAC, GPIO toggle, WiFi/BLE char dispatch.
- **Inter-Component Linking**: Bezier curve connections connecting typed output handles to input handles with validation (e.g. float to float).
- **Live State Inspection**: Nodes display live values directly on their face as packets arrive.

## 3. UI / Layout Requirements
- Center dominant workspace with a top block palette (quick-add buttons or drag-and-drop toolbar).
- Minimap toggle and zoom controls (suitable for both desktop mouse wheel and mobile pinch-to-zoom).
- Contextual properties panel when a node is selected (to edit min/max limits, pins, or channels).

