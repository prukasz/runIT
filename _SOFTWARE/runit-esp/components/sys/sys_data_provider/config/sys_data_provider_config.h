#pragma once

/**
 * @file sys_data_provider_config.h
 * @brief What a provider's id and state are, the one place they are assigned (wire format: never renumber).
 * Included by sys_data_provider.h; published to the app by templates/generate_enums.py.
 */

/** @brief Which driver made a provider: the `link` of its id. 0 is invalid. */
//#ref-enum @alias Link
typedef enum {
  SYS_DATA_PROVIDER_LINK_UART = 1,  //@alias Serial @description A UART port
  SYS_DATA_PROVIDER_LINK_BLE  = 2,  //@alias Bluetooth LE @description A BLE characteristic, or the RX / TX pair
  // 3 is kept for MQTT over Wi-Fi: listed here once it has a driver (the app offers every link listed).
} sys_data_provider_link_e;

/* A provider's state: the provider's `state` field, sent as is in LIST. FRAMED and WIRED are its driver's, set
   before add; the rest the registry's. */
//#ref-defines prov_state @alias Provider state @prefix SYS_DATA_PROVIDER_STATE_
#define SYS_DATA_PROVIDER_STATE_SYSTEM (1u << 0)  //@alias System @description The board's own: not removed
#define SYS_DATA_PROVIDER_STATE_OPEN   (1u << 1)  //@alias Open @description Taken by its one user
#define SYS_DATA_PROVIDER_STATE_FRAMED (1u << 2)  //@alias Framed @description Its driver splits and joins frames
#define SYS_DATA_PROVIDER_STATE_WIRED  (1u << 3)  //@alias Wired @description A cable (UART, SPI): a round trip of a millisecond
