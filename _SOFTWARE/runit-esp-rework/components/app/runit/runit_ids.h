#pragma once

/* The board's own system devices and providers: never removed (SYS_DEVICE_CREATE_SYSTEM, STATE_SYSTEM). */
#define RUNIT_DEV_GPIO      0x00u
#define RUNIT_DEV_TYPE_GPIO 0x0040u  // device_esp32s3_gpio (0-byte CREATE config)

#define RUNIT_PROVIDER_CONSOLE 0x01u  // the console (UART0 + USB-Serial-JTAG): a client's, or a board's, session
#define RUNIT_PROVIDER_BLE     0x03u  // the runIT pair of BLE: a client's way
#define RUNIT_PROVIDER_BLE_LOGS 0x04u  // the BLE characteristic of the logs: raw, the stream LOGS goes to it, no session

/* The runIT GATT service: the client's session. The board's provider of it is made in connectivity/runit_ways.c. */
//#ref-defines ble_runit_uuid @alias runIT GATT UUIDs @prefix RUNIT_BLE_
#define RUNIT_BLE_SERVICE 0xFFE0  //@alias runIT service @description The board's own service: the client's session
#define RUNIT_BLE_TX      0xFFE1  //@alias Session out @description Notify, framed: the session's frames to the client
#define RUNIT_BLE_RX      0xFFE2  //@alias Session in @description Write, framed: the session's frames from the client
#define RUNIT_BLE_LOGS_SERVICE 0xFFE8  //@alias Logs service @description The board's own: the log stream as text
#define RUNIT_BLE_LOGS    0xFFE9  //@alias Logs @description Notify, raw: the log lines, no session
