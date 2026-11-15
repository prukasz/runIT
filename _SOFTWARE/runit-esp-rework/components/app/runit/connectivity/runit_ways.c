#include "runit_connectivity.h"

#include "runit_ids.h"
#include "sys_ble.h"
#include "sys_data_provider.h"
#include "sys_session.h"

err_h runit_ways_init(void) {
  ERR_TRY(sys_data_provider_create_system(RUNIT_PROVIDER_CONSOLE, SYS_DATA_PROVIDER_TYPE_CONSOLE, NULL, 0u));
  ERR_TRY(sys_session_attach(RUNIT_PROVIDER_CONSOLE, SYS_SESSION_UNKNOWN));
  ERR_TRY(sys_ble_init());
  const uint16_t pair[2] = {SYS_BLE_CHR_RUNIT_TX, SYS_BLE_CHR_RUNIT_RX};  // provider_ble's cfg: tx, rx
  ERR_TRY(sys_data_provider_create_system(RUNIT_PROVIDER_BLE, SYS_DATA_PROVIDER_TYPE_BLE, pair, sizeof(pair)));
  return sys_session_attach(RUNIT_PROVIDER_BLE, SYS_SESSION_UNKNOWN);
}
