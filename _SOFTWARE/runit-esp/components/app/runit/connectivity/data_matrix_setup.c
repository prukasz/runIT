#include "runit_connectivity.h"

#include "errors_id.h"
#include "provider_ble.h"
#include "provider_console.h"
#include "router_id.h"
#include "runit_ids.h"
#include "sdkconfig.h"
#include "sys_ble.h"
#include "sys_data_provider.h"
#include "sys_device_remote_id.h"
#include "sys_event_id.h"
#include "sys_log_id.h"
#include "sys_session.h"
#include "sys_watch_id.h"
#include "vm_id.h"
#include "utils.h"

static const provider_console_cfg_t k_console = {.rx_flags = CMD_SEND_NOWAIT};
// The characteristics of both providers are in the one runIT service, made below.
static const provider_ble_cfg_t k_ble = {
    .svc = RUNIT_BLE_SERVICE, .chr = RUNIT_BLE_SESSION, .flags = PROVIDER_BLE_FRAMED,
    .rx_flags = CMD_SEND_NOWAIT, .name = "runit"};
static const provider_ble_cfg_t k_ble_logs = {
    .svc = RUNIT_BLE_SERVICE, .chr = RUNIT_BLE_LOGS, .name = "logs"};
static const provider_ble_cfg_t k_ble_telemetry = {
    .svc = RUNIT_BLE_SERVICE, .chr = RUNIT_BLE_TELEMETRY, .name = "telemetry"};

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
    {RUNIT_PROVIDER_BLE_TELEMETRY, SYS_DATA_PROVIDER_TYPE_BLE, &k_ble_telemetry, sizeof(k_ble_telemetry)},
};

// A sub-port of a session and its links: IN, the sub-port to its module's port (what the peer sends: commands, other
// boards' records and events, entries); OUT, the module's port to the sub-port (what the peer hears).
#define IN  1u
#define OUT 2u

typedef struct {
  uint8_t module;
  uint8_t ways;
} sub_row_t;

// The runIT BLE pair (the app): its commands, the errors, the events and the remote devices' frames; logs and
// telemetry have their own characteristics, raw. REMOTE is IN only: its frames out go to the sub-port by name. VM is OUT only: the program's telemetry.
static const sub_row_t k_ble_subs[] = {
    {MODULE_ID_ROUTER, IN}, {MODULE_ID_ERRORS, IN | OUT}, {MODULE_ID_SYS_EVENT, IN | OUT}, {MODULE_ID_REMOTE, IN},
    {MODULE_ID_VM, OUT}};

// The console (the app or a board): everything, the logs, the watches' entries and the remote devices' frames too.
static const sub_row_t k_console_subs[] = {
    {MODULE_ID_ROUTER, IN},      {MODULE_ID_ERRORS, IN | OUT}, {MODULE_ID_SYS_EVENT, IN | OUT},
    {MODULE_ID_WATCH, IN | OUT}, {MODULE_ID_LOG, OUT},         {MODULE_ID_REMOTE, IN},
    {MODULE_ID_VM, OUT}};

// A board's session (a slave's or a master's, made at run time by ATTACH with SYS_SESSION_SLAVE / _MASTER): the same
// rows for every board, the records going up the tree. A slave's errors, events and entries come in and are passed on
// to the app's session (never back); this board's own go up to its master. Its commands come in, so does the remote
// devices' frames; a call to the peer goes to its router sub-port by name.
static const sys_session_sub_t k_slave_subs[] = {
    {MODULE_ID_ROUTER, SYS_SESSION_IN}, {MODULE_ID_ERRORS, SYS_SESSION_IN}, {MODULE_ID_SYS_EVENT, SYS_SESSION_IN},
    {MODULE_ID_WATCH, SYS_SESSION_IN},  {MODULE_ID_REMOTE, SYS_SESSION_IN}};
static const sys_session_sub_t k_master_subs[] = {
    {MODULE_ID_ROUTER, SYS_SESSION_IN},  {MODULE_ID_ERRORS, SYS_SESSION_OUT}, {MODULE_ID_SYS_EVENT, SYS_SESSION_OUT},
    {MODULE_ID_WATCH, SYS_SESSION_OUT},  {MODULE_ID_REMOTE, SYS_SESSION_IN}};

// A session of the board's own, with the dead-man's default (a safety one when there is a time).
typedef struct {
  uint8_t provider;
  const sub_row_t* subs;
  uint8_t count;
} session_row_t;

static const session_row_t k_sessions[] = {
    {RUNIT_PROVIDER_CONSOLE, k_console_subs, ARRAY_LEN(k_console_subs)},
    {RUNIT_PROVIDER_BLE, k_ble_subs, ARRAY_LEN(k_ble_subs)},
};
#define SESSION_FLAGS (CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS > 0 ? SYS_SESSION_SAFETY : 0u)

#define MODULE_PORT(module) DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, (module))

static err_h connect(data_matrix_port_id_t from, data_matrix_port_id_t to) {
  return data_matrix_connect(&(data_matrix_link_t){.from = from, .to = to});
}

// A session of the board's own: attached with its sub-ports, each linked its ways.
static err_h attach(const session_row_t* row) {
  uint8_t modules[CONFIG_SYS_SESSION_PORTS];
  for (uint32_t m = 0; m < row->count; m++) {
    modules[m] = row->subs[m].module;
  }
  ERR_TRY(sys_session_attach(row->provider, CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS, SESSION_FLAGS, modules, row->count));
  for (uint32_t m = 0; m < row->count; m++) {
    const data_matrix_port_id_t sub = SYS_SESSION_PORT(row->provider, row->subs[m].module);
    const data_matrix_port_id_t module = MODULE_PORT(row->subs[m].module);
    if (row->subs[m].ways & IN) {
      ERR_TRY(connect(sub, module));
    }
    if (row->subs[m].ways & OUT) {
      ERR_TRY(connect(module, sub));
    }
  }
  return NULL;
}

// The board's data (RUNIT.MD "The board's data"): the providers, the sessions and their links, then BLE, once its
// characteristics are in the table. The modules' ports are registered by their modules before (runit.c).
err_h runit_data_matrix_setup(void) {
  ERR_TRY(sys_session_set_boards(k_slave_subs, ARRAY_LEN(k_slave_subs), k_master_subs, ARRAY_LEN(k_master_subs)));
  ERR_TRY(sys_ble_service_create(RUNIT_BLE_SERVICE, true));  // the providers' characteristics join it
  for (uint32_t i = 0; i < ARRAY_LEN(k_providers); i++) {
    ERR_TRY(sys_data_provider_create_system(k_providers[i].id, k_providers[i].type, k_providers[i].cfg,
                                            k_providers[i].cfg_len));
  }
  for (uint32_t i = 0; i < ARRAY_LEN(k_sessions); i++) {
    ERR_TRY(attach(&k_sessions[i]));
  }
  // The raw characteristics, with no session: the log lines as text, the watches' entries as they are.
  ERR_TRY(connect(MODULE_PORT(MODULE_ID_LOG), DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, RUNIT_PROVIDER_BLE_LOGS)));
  ERR_TRY(connect(MODULE_PORT(MODULE_ID_WATCH),
                  DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, RUNIT_PROVIDER_BLE_TELEMETRY)));
  // The board's own service: nothing is added or removed after its characteristics.
  ERR_TRY(sys_ble_service_lock(RUNIT_BLE_SERVICE));
  return sys_ble_start(RUNIT_BLE_SERVICE);
}
