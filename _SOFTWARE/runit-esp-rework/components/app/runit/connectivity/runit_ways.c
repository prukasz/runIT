#include "runit_connectivity.h"

#include "data_matrix.h"
#include "runit_ids.h"
#include "sys_ble.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_log.h"
#include "sys_session.h"

/* The CREATE config of provider_ble (a provider has no public header): the service, the TX and RX characteristics it
   makes, its flags, the name of the characteristics. */
typedef struct __attribute__((packed)) {
  uint16_t svc;
  uint16_t tx;
  uint16_t rx;
  uint8_t flags;
  char name[16];
} ble_provider_cfg_t;

_Static_assert(sizeof(ble_provider_cfg_t) == 23u, "provider_ble create cfg");

#define BLE_FRAMED (1u << 0)  // the characteristics carry frames: a session's way
#define BLE_LOCKED (1u << 1)  // the service is the board's own: nothing is added to it or removed

// The dead-man of the console's and the BLE's sessions at boot: a safety one when there is a time (Kconfig).
static const uint8_t k_session_flags = CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS > 0 ? SYS_SESSION_SAFETY : 0u;

// A provider of a client's way: a session, and the streams the app hears (the errors and the events) pushed on it.
static err_h session_way(uint8_t provider) {
  const data_matrix_role_args_t role = {.provider = provider,
                                        .role = DATA_MATRIX_ROLE_SESSION,
                                        .deadman_ms = CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS,
                                        .flags = k_session_flags};
  ERR_TRY(data_matrix_set_role(&role));
  const uint8_t streams[] = {LOG_STREAM_ERRORS, RUNIT_STREAM_EVENTS};
  for (uint32_t i = 0; i < sizeof(streams); i++) {
    const data_matrix_route_t route = {DATA_MATRIX_END_STREAM(streams[i]), DATA_MATRIX_END_TAGGED(provider, streams[i])};
    ERR_TRY(data_matrix_route(&route));
  }
  return NULL;
}

err_h runit_ways_init(void) {
  ERR_TRY(sys_data_provider_create_system(RUNIT_PROVIDER_CONSOLE, SYS_DATA_PROVIDER_TYPE_CONSOLE, NULL, 0u));
  ERR_TRY(session_way(RUNIT_PROVIDER_CONSOLE));
  const ble_provider_cfg_t ble = {
      .svc = RUNIT_BLE_SERVICE, .tx = RUNIT_BLE_TX, .rx = RUNIT_BLE_RX, .flags = BLE_FRAMED | BLE_LOCKED,
      .name = "runit"};
  ERR_TRY(sys_data_provider_create_system(RUNIT_PROVIDER_BLE, SYS_DATA_PROVIDER_TYPE_BLE, &ble, sizeof(ble)));
  ERR_TRY(session_way(RUNIT_PROVIDER_BLE));
  // The logs as text on a characteristic of their own, with no session: whoever is connected hears them.
  const ble_provider_cfg_t logs = {.svc = RUNIT_BLE_LOGS_SERVICE, .tx = RUNIT_BLE_LOGS, .flags = BLE_LOCKED, .name = "logs"};
  ERR_TRY(sys_data_provider_create_system(RUNIT_PROVIDER_BLE_LOGS, SYS_DATA_PROVIDER_TYPE_BLE, &logs, sizeof(logs)));
  ERR_TRY(data_matrix_set_role(&(data_matrix_role_args_t){.provider = RUNIT_PROVIDER_BLE_LOGS, .role = DATA_MATRIX_ROLE_RAW}));
  ERR_TRY(data_matrix_route(&(data_matrix_route_t){DATA_MATRIX_END_STREAM(LOG_STREAM_LOGS),
                                                    DATA_MATRIX_END_PROVIDER(RUNIT_PROVIDER_BLE_LOGS)}));
  return sys_ble_start(RUNIT_BLE_SERVICE);  // after the providers: their characteristics are in the table
}
