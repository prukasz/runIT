#pragma once

/**
 * @file router_ids.h
 * @brief Root router keys - the one place they are assigned (wire format: never renumber, add new ones at the
 * end, 0 is invalid). The app layer binds a key to a module router with router_register (runit.c); modules don't
 * register themselves. ROUTER_KEY_EVENT is only the shape of the events stream: it has no router. */

/* Root keys: first key of every command, after the session's frame header. */
//#ref-defines router_key @alias Command Target @prefix ROUTER_KEY_
#define ROUTER_KEY_SYS      0x01u  //@alias System @description System: ping, stop, restart, dead-man, clock, safe state
#define ROUTER_KEY_I2C      0x02u  //@alias I2C @description I2C: bus diagnostics (scan, probe)
#define ROUTER_KEY_BLE      0x03u  //@alias Bluetooth @description Bluetooth: runtime GATT table and BLE status
#define ROUTER_KEY_CONN     0x04u  //@alias Data streams @description Data streams: create, bind, suspend and list outbound streams
#define ROUTER_KEY_DEVICE   0x05u  //@alias Devices @description Devices: create, remove and use devices
#define ROUTER_KEY_ACTIONS  0x06u  //@alias Actions @description Actions: stored command lists (run, record, upload, read back)
#define ROUTER_KEY_EVENT    0x07u  //@alias Events @description Events: frame shape of the events stream (no commands)
#define ROUTER_KEY_LOG      0x08u  //@alias Log @description Log: log output options
// 0x09 is unused: never assign it.
#define ROUTER_KEY_POWER    0x0Au  //@alias Power manager @description Power manager: power budget, supply, battery and event responses
#define ROUTER_KEY_VM       0x0Bu  //@alias Program @description Program: build, run and watch the VM program
#define ROUTER_KEY_PROJECT  0x0Cu  //@alias Project @description Project: save a whole project as one stream, run it, read state
#define ROUTER_KEY_PROVIDER 0x0Du  //@alias Providers @description Providers: the ways bytes go out and come in (list, remove, send)
#define ROUTER_KEY_SESSION  0x0Eu  //@alias Sessions @description Sessions: this board's id, its peers (attach, detach, list)
