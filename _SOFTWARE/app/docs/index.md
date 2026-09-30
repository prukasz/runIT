# runIT Studio design notes

This directory holds the app-level contracts that are shared by the UI,
project compiler, and platform adapters.  The app is desktop-web first and
must not bind its domain or protocol code to a particular transport.

* [BLE transport backend](02_packet_protocol_and_errors.md)
* [Device-definition packages](01_digital_twin_and_state.md)
* [Project store: the board keeps the project as frames](03_records_as_storage.md) (built)
* [Device user guides](devices/) — one page per device type (`<descriptor id>.md`), shown on the device's User guide tab
