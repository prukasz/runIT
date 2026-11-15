#include "runit_connectivity.h"

#include "runit_ids.h"
#include "sys_ble.h"
#include "sys_data_provider.h"
#include "sys_session.h"
#include "sys_stream.h"

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

err_h runit_ways_init(void) {
  ERR_TRY(sys_data_provider_create_system(RUNIT_PROVIDER_CONSOLE, SYS_DATA_PROVIDER_TYPE_CONSOLE, NULL, 0u));
  ERR_TRY(sys_session_attach(RUNIT_PROVIDER_CONSOLE, SYS_SESSION_UNKNOWN));
  const ble_provider_cfg_t ble = {
      .svc = RUNIT_BLE_SERVICE, .tx = RUNIT_BLE_TX, .rx = RUNIT_BLE_RX, .flags = BLE_FRAMED | BLE_LOCKED,
      .name = "runit"};
  ERR_TRY(sys_data_provider_create_system(RUNIT_PROVIDER_BLE, SYS_DATA_PROVIDER_TYPE_BLE, &ble, sizeof(ble)));
  ERR_TRY(sys_session_attach(RUNIT_PROVIDER_BLE, SYS_SESSION_UNKNOWN));
  // The logs as text on a characteristic of their own, with no session: the stream LOGS's sink, whoever is connected.
  const ble_provider_cfg_t logs = {.svc = RUNIT_BLE_LOGS_SERVICE, .tx = RUNIT_BLE_LOGS, .flags = BLE_LOCKED, .name = "logs"};
  ERR_TRY(sys_data_provider_create_system(RUNIT_PROVIDER_BLE_LOGS, SYS_DATA_PROVIDER_TYPE_BLE, &logs, sizeof(logs)));
  ERR_TRY(sys_stream_sink(SYS_STREAM_LOGS, SYS_SESSION_ID_SELF, RUNIT_PROVIDER_BLE_LOGS));
  return sys_ble_start(RUNIT_BLE_SERVICE);  // after the providers: their characteristics are in the table
}
