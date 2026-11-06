#pragma once

/**
 * @file sys_data_provider_config.h
 * @brief Provider state flags and link types (UART, MQTT, BLE...)
 */

/** @brief TO wich driver provider belngs to*/
//#ref-enum @alias Link
typedef enum {
  SYS_DATA_PROVIDER_LINK_UART = 1,  //@alias Serial @description A UART port
  SYS_DATA_PROVIDER_LINK_BLE  = 2,  //@alias Bluetooth LE @description A BLE characteristic, or the RX / TX pair
  SYS_DATA_PROVIDER_LINK_MQTT = 3,  //@alias MQTT @description a ble topic sub pub 
} sys_data_provider_link_e;

/* A provider's state: the provider's `state` field, sent as is in LIST. SYSTEM, WIRED and FRAMED are its driver's, set before
   add, UP too, then by sys_data_provider_set_up(); OPEN the registry's. */
//#ref-defines prov_state @alias Provider state @prefix SYS_DATA_PROVIDER_STATE_
#define SYS_DATA_PROVIDER_STATE_SYSTEM (1u << 0)  //@alias System @description The board's own: not removed
#define SYS_DATA_PROVIDER_STATE_OPEN   (1u << 1)  //@alias Open @description Taken by its one user
#define SYS_DATA_PROVIDER_STATE_WIRED  (1u << 3)  //@alias Wired @description Connection realised by cable - low latency 
#define SYS_DATA_PROVIDER_STATE_UP     (1u << 4)  //@alias Up @description It delivers frames now
#define SYS_DATA_PROVIDER_STATE_FRAMED (1u << 5)  //@alias Framed @description Its driver carries a whole CMD_FRAME_MAX frame; else raw: what the link takes at once
