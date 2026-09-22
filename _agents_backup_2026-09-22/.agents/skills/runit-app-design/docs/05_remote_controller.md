# Element 05: Remote Controller & Gamepad Dashboard

## 1. Concept Overview
A configurable touch- and mouse-friendly control panel allowing direct manipulation of hardware variables, actuators, and blocks using virtual widgets and physical gamepads.

## 2. Key Responsibilities
- **Configurable Widgets**:
  - Sliders (linear range controls for PWM, throttle, frequencies).
  - Toggles & Push Buttons (momentary or latching switches for digital outputs, relays, power gates).
  - Virtual 2-Axis Joysticks (circular touch/mouse thumbsticks with spring-return and deadband).
  - Telemetry Gauges (radial dials or progress bars for voltages, temps, RSSI).
- **Gamepad API Integration**:
  - Polls `navigator.getGamepads()` at 60Hz.
  - Interactive binding modal: user moves a physical joystick or button on their Xbox/PlayStation/DirectInput pad to pair it to a variable or virtual slider.
- **Direct Variable Linking**: Any control can be bound directly to a digital twin variable address.

## 3. UI / Layout Requirements
- Right-side collapsible drawer (on Desktop) or dedicated full tab (on Mobile).
- Touch-optimized hit targets (minimum 44x44px for sliders and buttons).
- Emergency stop toggle button always pinned at the top or bottom of the panel.

