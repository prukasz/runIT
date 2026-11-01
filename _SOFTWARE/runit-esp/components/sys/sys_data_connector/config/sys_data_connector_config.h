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

/* A binding's flags: which way it carries. Only TX is taken: a connector keeps nothing that comes in. */
//#ref-defines conn_bind @alias Binding @prefix SYS_DATA_CONNECTOR_BIND_
#define SYS_DATA_CONNECTOR_BIND_TX (1u << 1)  //@alias Send @description What it sends goes out on the provider

/* A connector's flags, set at create. URGENT: its frames go to the front of every provider's queue. */
//#ref-defines conn_flag @alias Connector flags @prefix SYS_DATA_CONNECTOR_FLAG_
#define SYS_DATA_CONNECTOR_FLAG_URGENT (1u << 0)  //@alias Urgent @description Its frames go first on every way out

/* A connector's state, in LIST. */
//#ref-defines conn_state @alias Connector state @prefix SYS_DATA_CONNECTOR_STATE_
#define SYS_DATA_CONNECTOR_STATE_SYSTEM    (1u << 0)  //@alias System @description The board's own: not removed
#define SYS_DATA_CONNECTOR_STATE_SUSPENDED (1u << 1)  //@alias Suspended @description Sends dropped
