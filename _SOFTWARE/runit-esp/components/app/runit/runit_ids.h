#pragma once

/* The board's own system devices and providers: never removed (SYS_DEVICE_CREATE_SYSTEM, STATE_SYSTEM). */
#define RUNIT_DEV_GPIO      0x00u
#define RUNIT_DEV_TYPE_GPIO 0x0040u  // device_esp32s3_gpio (0-byte CREATE config)

#define RUNIT_PROVIDER_CONSOLE       0x01u  // the console (UART0 + USB-Serial-JTAG): a client's, or a board's, session
#define RUNIT_PROVIDER_BLE           0x03u  // the runIT pair of BLE: a client's session
#define RUNIT_PROVIDER_BLE_LOGS      0x04u  // the BLE characteristic of the logs: raw, LOGS linked to it
#define RUNIT_PROVIDER_BLE_TELEMETRY 0x05u  // the BLE characteristic of the telemetry: raw, WATCH linked to it

/* The runIT GATT service: the client's session. The board's provider of it is in connectivity/data_matrix_setup.c. */
//#ref-defines ble_runit_uuid @alias runIT GATT UUIDs @prefix RUNIT_BLE_
#define RUNIT_BLE_SERVICE   0xFFE0  //@alias runIT service @description The board's own service: the client's session
#define RUNIT_BLE_TX        0xFFE1  //@alias Session out @description Notify, framed: the session's frames to the client
#define RUNIT_BLE_RX        0xFFE2  //@alias Session in @description Write, framed: the session's frames from the client
#define RUNIT_BLE_LOGS      0xFFE3  //@alias Logs @description Notify, raw: the log lines, no session
#define RUNIT_BLE_TELEMETRY 0xFFE4  //@alias Telemetry @description Notify, raw: the watches' entries ([hdr PUSH][owner][id][status][data]), no session
