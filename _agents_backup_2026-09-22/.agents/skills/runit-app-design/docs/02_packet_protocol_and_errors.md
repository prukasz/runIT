# Element 02: Packet Protocol & Runtime Error Mapping

## 1. Concept Overview
Handles binary packet transport over BLE and WiFi, decoding telemetry streams, and correlating raw error codes to specific blocks, objects, and pins in the UI.

## 2. Key Responsibilities
- **Abstract Transport Interface**: Common API (`connect`, `disconnect`, `send`, `onPacket`) implemented for Web Bluetooth (`navigator.bluetooth`), WiFi sockets, and later native Android BLE.
- **Packet Serialization / Deserialization**: Binary framing, header verification, payload unpacking, and CRC validation (matching existing `decoder_types.py`).
- **Error Mapping Engine**:
  - Translates raw system errors (from `components/sys_errors/sys_error_handler.c`) into human-readable alerts.
  - Pinpoints the exact origin: if an I2C NACK occurs on bus 1, maps the error indicator directly onto the `PCA9685` tree node, the canvas block, and the corresponding I2C pins on the board SVG.

## 3. UI / Layout Requirements
- Top navigation connection pill: displays transport mode (BLE / WiFi / Offline), RSSI, device name, and connection latency.
- Collapsible bottom drawer: live hexadecimal & decoded packet stream with search, filtering by packet type, and auto-scroll freeze.
- Inline visual error badges: red/yellow hazard icons next to affected blocks and pins.

