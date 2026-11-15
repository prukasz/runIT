#pragma once

/* The board's own system devices and providers: never removed (SYS_DEVICE_CREATE_SYSTEM, STATE_SYSTEM). */
#define RUNIT_DEV_GPIO      0x00u
#define RUNIT_DEV_TYPE_GPIO 0x0040u  // device_esp32s3_gpio (0-byte CREATE config)

#define RUNIT_PROVIDER_CONSOLE 0x01u  // the console (UART0 + USB-Serial-JTAG): a client's or the master's way
#define RUNIT_PROVIDER_BLE     0x03u  // the runIT pair of BLE: a client's way
