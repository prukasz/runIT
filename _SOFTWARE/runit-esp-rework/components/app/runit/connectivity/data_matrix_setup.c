#include "runit_connectivity.h"

#include "data_matrix.h"
#include "provider_ble.h"
#include "runit_ids.h"
#include "sdkconfig.h"
#include "sys_ble.h"
#include "sys_data_provider.h"
#include "sys_event_stream.h"
#include "sys_log.h"
#include "sys_session.h"
#include "utils.h"

// The characteristics of both providers are in the one runIT service, made below.
static const provider_ble_cfg_t k_ble = {
    .svc = RUNIT_BLE_SERVICE, .tx = RUNIT_BLE_TX, .rx = RUNIT_BLE_RX, .flags = PROVIDER_BLE_FRAMED, .name = "runit"};
static const provider_ble_cfg_t k_ble_logs = {
    .svc = RUNIT_BLE_SERVICE, .tx = RUNIT_BLE_LOGS, .name = "logs"};

// The dead-man of the console's and the BLE's sessions at boot: a safety one when there is a time (Kconfig).
#define SESSION_ROLE(id)                                                                                       {.provider = (id), .role = DATA_MATRIX_ROLE_SESSION, .deadman_ms = CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS,    .flags = CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS > 0 ? SYS_SESSION_SAFETY : 0u}

// What the app hears on a session (the errors, the events) is pushed on it.
#define APP_STREAMS(id)                                                                                  {DATA_MATRIX_END_STREAM(LOG_STREAM_ERRORS), DATA_MATRIX_END_TAGGED((id), LOG_STREAM_ERRORS)},          {DATA_MATRIX_END_STREAM(EVENT_STREAM_NOTIFY), DATA_MATRIX_END_TAGGED((id), EVENT_STREAM_NOTIFY)}

// A provider of the board's own: its id, its type and the driver's CREATE config (none: NULL).
typedef struct {
  uint8_t id;
  uint8_t type;
  const void* cfg;
  uint8_t cfg_len;
} provider_row_t;

static const provider_row_t k_providers[] = {
    {RUNIT_PROVIDER_CONSOLE, SYS_DATA_PROVIDER_TYPE_CONSOLE, NULL, 0},
    {RUNIT_PROVIDER_BLE, SYS_DATA_PROVIDER_TYPE_BLE, &k_ble, sizeof(k_ble)},
    {RUNIT_PROVIDER_BLE_LOGS, SYS_DATA_PROVIDER_TYPE_BLE, &k_ble_logs, sizeof(k_ble_logs)},
};

static const data_matrix_role_args_t k_roles[] = {
    SESSION_ROLE(RUNIT_PROVIDER_CONSOLE),
    SESSION_ROLE(RUNIT_PROVIDER_BLE),
    {.provider = RUNIT_PROVIDER_BLE_LOGS, .role = DATA_MATRIX_ROLE_RAW},  // the logs as text, with no session
};

static const data_matrix_route_t k_routes[] = {
    APP_STREAMS(RUNIT_PROVIDER_CONSOLE),
    APP_STREAMS(RUNIT_PROVIDER_BLE),
    {DATA_MATRIX_END_STREAM(LOG_STREAM_LOGS), DATA_MATRIX_END_PROVIDER(RUNIT_PROVIDER_BLE_LOGS)},
};

// The board's data (RUNIT.MD "The board's data"): the matrix, the streams of its modules (sys_log, sys_event_stream),
// then the providers, their roles and routes from the tables above, and BLE last, once its characteristics are in the
// table.
err_h runit_data_matrix_setup(void) {
  ERR_TRY(data_matrix_init());
  ERR_TRY(sys_log_init());
  ERR_TRY(sys_event_stream_init());
  ERR_TRY(sys_ble_service_create(RUNIT_BLE_SERVICE, true));  // the providers' characteristics join it
  for (uint32_t i = 0; i < ARRAY_LEN(k_providers); i++) {
    ERR_TRY(sys_data_provider_create_system(k_providers[i].id, k_providers[i].type, k_providers[i].cfg,
                                            k_providers[i].cfg_len));
  }
  for (uint32_t i = 0; i < ARRAY_LEN(k_roles); i++) {
    ERR_TRY(data_matrix_set_role(&k_roles[i]));
  }
  for (uint32_t i = 0; i < ARRAY_LEN(k_routes); i++) {
    ERR_TRY(data_matrix_route(&k_routes[i]));
  }
  ERR_TRY(sys_ble_service_lock(RUNIT_BLE_SERVICE));  // the board's own: nothing is added or removed after its characteristics
  return sys_ble_start(RUNIT_BLE_SERVICE);
}
