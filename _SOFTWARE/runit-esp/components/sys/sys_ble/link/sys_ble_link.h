#pragma once

#include "errors.h"
#include "sys_ble.h"
#include "sys_data_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The runIT pair as a provider (named by its TX characteristic): the session of a BLE client. */
#define SYS_BLE_PROVIDER_RUNIT \
  ((sys_data_provider_id_t){.link = SYS_DATA_PROVIDER_LINK_BLE, .channel = SYS_BLE_CHR_RUNIT_TX})
/** The LOGS characteristic as a provider, out only: the board config binds the LOGS connector to it. */
#define SYS_BLE_PROVIDER_LOGS \
  ((sys_data_provider_id_t){.link = SYS_DATA_PROVIDER_LINK_BLE, .channel = SYS_BLE_CHR_RUNIT_LOGS})

/**
 * @brief Creates the runIT service (0xFFE0: TX 0xFFE1, RX 0xFFE2, LOGS 0xFFE3, STATUS 0xFFE4), locks it and adds two
 * framed providers: the runIT pair (SYS_BLE_PROVIDER_RUNIT: framed writes on RX are joined and delivered, frames go
 * out on TX) and LOGS (SYS_BLE_PROVIDER_LOGS). Both are up while a client is connected. Call before sys_ble_start().
 */
MUST_USE err_h sys_ble_link_init(void);

#ifdef __cplusplus
}
#endif
