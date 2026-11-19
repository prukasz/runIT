#include "runit_connectivity.h"

#include "data_matrix.h"
#include "errors_id.h"
#include "provider_ble.h"
#include "provider_console.h"
#include "router_queue.h"
#include "runit_ids.h"
#include "sdkconfig.h"
#include "sys_ble.h"
#include "sys_data_provider.h"
#include "sys_event_id.h"
#include "sys_event_stream.h"
#include "sys_log.h"
#include "sys_log_id.h"
#include "sys_remote_id.h"
#include "sys_session.h"
#include "sys_watch_id.h"
#include "utils.h"

static const provider_console_cfg_t k_console = {.rx_flags = CMD_SEND_NOWAIT};
// The characteristics of both providers are in the one runIT service, made below.
static const provider_ble_cfg_t k_ble = {
    .svc = RUNIT_BLE_SERVICE, .tx = RUNIT_BLE_TX, .rx = RUNIT_BLE_RX, .flags = PROVIDER_BLE_FRAMED,
    .rx_flags = CMD_SEND_NOWAIT, .name = "runit"};
static const provider_ble_cfg_t k_ble_logs = {
    .svc = RUNIT_BLE_SERVICE, .tx = RUNIT_BLE_LOGS, .name = "logs"};

// A provider of the board's own: its id, its type and the driver's CREATE config.
typedef struct {
  uint8_t id;
  uint8_t type;
  const void* cfg;
  uint8_t cfg_len;
} provider_row_t;

static const provider_row_t k_providers[] = {
    {RUNIT_PROVIDER_CONSOLE, SYS_DATA_PROVIDER_TYPE_CONSOLE, &k_console, sizeof(k_console)},
    {RUNIT_PROVIDER_BLE, SYS_DATA_PROVIDER_TYPE_BLE, &k_ble, sizeof(k_ble)},
    {RUNIT_PROVIDER_BLE_LOGS, SYS_DATA_PROVIDER_TYPE_BLE, &k_ble_logs, sizeof(k_ble_logs)},
};

// The sessions of the board's own: the console's and the runIT BLE pair's, with the dead-man's default (a safety one
// when there is a time).
static const uint8_t k_sessions[] = {RUNIT_PROVIDER_CONSOLE, RUNIT_PROVIDER_BLE};
#define SESSION_FLAGS (CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS > 0 ? SYS_SESSION_SAFETY : 0u)

// What a session shows: its commands and the modules a peer talks to. Each sub-port is linked to its module's port
// (what the peer sends: commands, other boards' records and events, entries, remote frames).
static const uint8_t k_modules[] = {MODULE_ID_ROUTER, MODULE_ID_ERRORS, MODULE_ID_SYS_EVENT, MODULE_ID_WATCH,
                                    MODULE_ID_REMOTE};

// The streams every session hears: linked from their module's port to the session's sub-port. The watches' entries and
// the remote frames go to one session by name, with no link.
static const uint8_t k_streams[] = {MODULE_ID_ERRORS, MODULE_ID_SYS_EVENT};

#define MODULE_PORT(module) DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, (module))

static err_h connect(data_matrix_port_id_t from, data_matrix_port_id_t to) {
  return data_matrix_connect(&(data_matrix_link_t){.from = from, .to = to});
}

// The board's data (RUNIT.MD "The board's data"): the ports of sys_log and the events, the providers, the sessions and
// their links, then BLE, once its characteristics are in the table. The router's, the watches' and the remote devices'
// ports are registered by their modules before.
err_h runit_data_matrix_setup(void) {
  ERR_TRY(sys_log_init());
  ERR_TRY(sys_event_stream_init());
  ERR_TRY(sys_ble_service_create(RUNIT_BLE_SERVICE, true));  // the providers' characteristics join it
  for (uint32_t i = 0; i < ARRAY_LEN(k_providers); i++) {
    ERR_TRY(sys_data_provider_create_system(k_providers[i].id, k_providers[i].type, k_providers[i].cfg,
                                            k_providers[i].cfg_len));
  }
  for (uint32_t i = 0; i < ARRAY_LEN(k_sessions); i++) {
    const uint8_t p = k_sessions[i];
    ERR_TRY(
        sys_session_attach(p, CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS, SESSION_FLAGS, k_modules, ARRAY_LEN(k_modules)));
    for (uint32_t m = 0; m < ARRAY_LEN(k_modules); m++) {
      ERR_TRY(connect(SYS_SESSION_PORT(p, k_modules[m]), MODULE_PORT(k_modules[m])));
    }
    for (uint32_t m = 0; m < ARRAY_LEN(k_streams); m++) {
      ERR_TRY(connect(MODULE_PORT(k_streams[m]), SYS_SESSION_PORT(p, k_streams[m])));
    }
  }
  // The log lines as text to whoever is connected to the BLE logs, with no session.
  ERR_TRY(connect(MODULE_PORT(MODULE_ID_LOG), DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, RUNIT_PROVIDER_BLE_LOGS)));
  // The board's own service: nothing is added or removed after its characteristics.
  ERR_TRY(sys_ble_service_lock(RUNIT_BLE_SERVICE));
  return sys_ble_start(RUNIT_BLE_SERVICE);
}
