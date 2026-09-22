# Element 04: Interactive Board SVG & Pinout Inspector

## 1. Concept Overview
An interactive vector representation of the physical runIT board (inspired by STM32CubeMX / Altium pinout views) showing the microcontroller, headers, connectors, and peripheral assignments.

## 2. Key Responsibilities
- **Interactive Pin Hit-Testing**: Each physical pin or connector terminal is an SVG element that can be clicked, hovered, or highlighted.
- **Pin Multiplexing & Capability Matrix**: Displays what peripherals are available on each pin (e.g. GPIO, ADC, PWM, I2C, SPI, UART).
- **Live Connection Feedback**:
  - Color codes pins based on current role (e.g. Green = I2C, Blue = PWM, Yellow = Power, Red = Error / Conflict).
  - Clicking a pin highlights its associated object in the tree and active block in the canvas.
- **Visual Error Indicators**: Glows or pulses red if a pin reports a hardware fault or packet collision.

## 3. UI / Layout Requirements
- Dockable either as a sub-panel under the Left Object Tree or as a dedicated Full-Screen View via top mode tabs.
- Pan & zoom support for detailed board inspection on high-DPI screens.
- Pin tooltip or inspector banner showing assigned accessor and live voltage/state.

