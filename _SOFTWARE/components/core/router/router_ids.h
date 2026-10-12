#pragma once

/**
 * @file router_ids.h
 * @brief Root router keys - the one place they are assigned. Never renumber;
 * add new ones at the end. 0 is invalid. Links are registered under an ID
 * the app assigns: today the transport IDs of sys_connector.h (transport_e),
 * which the router doesn't include or use.
 */

/* Root keys: first key of every command, after [seq][flags]. */
//#ref-defines router_key @alias Command Target @prefix ROUTER_KEY_
#define ROUTER_KEY_SYS 0x01u  //@alias System @description The board itself: ping, stop, restart, dead-man
#define ROUTER_KEY_I2C 0x02u  //@alias I2C @description I2C bus diagnostics: scan, probe
#define ROUTER_KEY_BLE 0x03u  //@alias Bluetooth @description The runtime GATT table and the BLE status
#define ROUTER_KEY_CONN 0x04u  //@alias Data streams @description Create, bind, suspend and list the outbound data streams
#define ROUTER_KEY_DEVICE 0x05u  //@alias Devices @description Create, remove and use devices: [id u16][op u16][args]
#define ROUTER_KEY_ACTIONS 0x06u  //@alias Actions @description Stored command lists: run, record, upload, read back
#define ROUTER_KEY_EVENT 0x07u  //@alias Events @description The frame shape of the events stream (no commands)
#define ROUTER_KEY_LOG 0x08u  //@alias Log @description Log output options
// 0x09 was the settings router (removed: settings are sys_actions records); the key stays unused.
#define ROUTER_KEY_POWER 0x0Au  //@alias Power manager @description Power budget, supply, battery and event responses
#define ROUTER_KEY_VM 0x0Bu  //@alias Program @description Build, run and watch the VM program; telemetry frames are these commands
#define ROUTER_KEY_PROJECT 0x0Cu  //@alias Project @description Save a whole project as one stream, run it, read its state
