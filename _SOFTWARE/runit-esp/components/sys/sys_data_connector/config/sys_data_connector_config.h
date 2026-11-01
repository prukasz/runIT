#pragma once

/**
 * @file sys_data_connector_config.h
 * @brief The system connectors, a connector's flags and a binding's: the one place they are assigned (wire format:
 * never renumber). Included by sys_data_connector.h; published to the app by templates/generate_enums.py.
 */

/* The system connectors (sys_data_connector_init): ERRORS and EVENTS urgent. A user connector takes any other byte but
   0x00, which is invalid, and 0x05, kept for the watches' entries. */
//#ref-defines connector @alias Connectors @prefix SYS_DATA_CONNECTOR_
#define SYS_DATA_CONNECTOR_TELEMETRY 0x02u  //@alias Telemetry @description Telemetry: the VM's subscribed values
#define SYS_DATA_CONNECTOR_LOGS      0x03u  //@alias Logs @description The log lines
#define SYS_DATA_CONNECTOR_ERRORS    0x04u  //@alias Errors @description The error records (urgent)
#define SYS_DATA_CONNECTOR_WATCHES   0x05u  //@alias Watches @description The watches' entries, in a stream frame to their owner (no connector of this byte exists)
#define SYS_DATA_CONNECTOR_EVENTS    0x06u  //@alias Events @description The event notifications (urgent)
#define SYS_DATA_CONNECTOR_EVENTS_ALL 0x07u  //@alias All events @description Every event but the local ones: what another board mirrors

/** @brief What a connector keeps of what comes in on its rx bindings (and on its mirror). */
//#ref-enum @alias Receive
typedef enum {
  SYS_DATA_CONNECTOR_RX_NONE   = 0,  //@alias None @description Nothing kept (send only, or forward only)
  SYS_DATA_CONNECTOR_RX_LATEST = 1,  //@alias Latest @description The last message, any number of readers
  SYS_DATA_CONNECTOR_RX_FIFO   = 2,  //@alias Queue @description Every message in order, one reader
} sys_data_connector_rx_e;

/* A binding's flags: which way it carries. */
//#ref-defines conn_bind @alias Binding @prefix SYS_DATA_CONNECTOR_BIND_
#define SYS_DATA_CONNECTOR_BIND_RX (1u << 0)  //@alias Receive @description It takes what comes in on the provider
#define SYS_DATA_CONNECTOR_BIND_TX (1u << 1)  //@alias Send @description What it sends goes out on the provider

/* A connector's flags, set at create. URGENT: its frames go to the front of every provider's queue. FORWARD: what
   comes in on one binding goes out to every other target. */
//#ref-defines conn_flag @alias Connector flags @prefix SYS_DATA_CONNECTOR_FLAG_
#define SYS_DATA_CONNECTOR_FLAG_URGENT  (1u << 0)  //@alias Urgent @description Its frames go first on every way out
#define SYS_DATA_CONNECTOR_FLAG_FORWARD (1u << 1)  //@alias Forward @description What comes in goes out on the others

/* A connector's state, in LIST. */
//#ref-defines conn_state @alias Connector state @prefix SYS_DATA_CONNECTOR_STATE_
#define SYS_DATA_CONNECTOR_STATE_SYSTEM    (1u << 0)  //@alias System @description The board's own: not removed
#define SYS_DATA_CONNECTOR_STATE_SUSPENDED (1u << 1)  //@alias Suspended @description Sends dropped
