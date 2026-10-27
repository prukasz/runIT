#pragma once

/**
 * @file sys_data_connector_config.h
 * @brief Transport IDs and the system connectors - the one place they are assigned (wire format: never
 * renumber). Included by sys_data_connector.h; published to the app by templates/generate_enums.py.
 */

/**
 * @brief Transport IDs. A transport is a router link (commands in, answers out) and a data connector provider
 * (streams out) under the same ID: the router takes any link ID in 1..CONFIG_ROUTER_MAX_LINKS and knows nothing of
 * this list. 0 is invalid.
 */
//#ref-enum @alias Transport
typedef enum {
  TRANSPORT_UART = 1,  //@alias Serial @description Serial console UART
  TRANSPORT_BLE  = 2,  //@alias Bluetooth LE @description Bluetooth Low Energy
  // 3 is kept for MQTT over Wi-Fi: listed here once it has a provider (the app offers every transport listed).
} transport_e;

/* The system connectors (created by sys_data_connector_init). A connector is one byte, which is also the first byte of every
   frame it sends (its stream byte); a user connector takes any other byte except the router's answers'
   (CONFIG_ROUTER_RESPONSE_STREAM, router Kconfig). */
//#ref-defines connector @alias Connectors @prefix SYS_DATA_CONNECTOR_
#define SYS_DATA_CONNECTOR_TELEMETRY 0x02u  //@alias Telemetry @description Telemetry: the VM's subscribed values
#define SYS_DATA_CONNECTOR_LOGS      0x03u  //@alias Logs @description The log lines
#define SYS_DATA_CONNECTOR_ERRORS    0x04u  //@alias Errors @description The error records
#define SYS_DATA_CONNECTOR_EVENTS    0x06u  //@alias Events @description The event notifications
