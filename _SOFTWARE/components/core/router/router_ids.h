#pragma once

/**
 * @file router_ids.h
 * @brief Root router keys - the one place they are assigned. Never renumber;
 * add new ones at the end. 0 is invalid. Links are registered under an ID
 * the app assigns: today the transport IDs of sys_connector.h (transport_e),
 * which the router doesn't include or use.
 */

/* Root keys: first key of every command, after [seq][flags]. */
#define ROUTER_KEY_SYS 0x01u  // router itself: ping, stop, restart, dead-man (router_sys.c)
#define ROUTER_KEY_I2C 0x02u  // sys_i2c diagnostics (sys_i2c_router.c)
#define ROUTER_KEY_BLE 0x03u   // runtime GATT table, BLE status (sys_ble_router.c)
#define ROUTER_KEY_CONN 0x04u  // data connectors: create, bind, suspend, info (sys_connector_router.c)
#define ROUTER_KEY_DEVICE 0x05u  // devices: [id u16][op u16][args] (sys_device_router.c)
#define ROUTER_KEY_ACTIONS 0x06u  // stored frame lists: run, record, upload, read (sys_actions_router.c)
#define ROUTER_KEY_EVENTS 0x07u   // event rules (event -> action), test publish (sys_event_router.c)
