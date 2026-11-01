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

/**
 * @brief Makes a provider of a characteristic created at run time, named by it ({BLE, uuid}): unframed, so each write
 * is one message (at most the MTU less the ATT header) and each send one notification. A characteristic that writes
 * with an RX handler (no RX buffer) is an rx provider, one with a TX buffer a tx provider. A framed one, or one with
 * neither, gets none (NULL). Up while a client is connected. Call after sys_ble_char_create().
 * @param svc_uuid  Its service.
 * @param cfg       What was created.
 * @return NULL; ERR_BLE_NO_MEM (CONFIG_SYS_BLE_USER_PROVIDERS taken), ERR_PROV_EXISTS, ERR_PROV_FULL.
 */
MUST_USE err_h sys_ble_link_char_add(uint16_t svc_uuid, const sys_ble_char_cfg_t* cfg);

/**
 * @brief Removes the provider of a characteristic (none: NULL). Call before sys_ble_char_remove(): a provider a
 * connector or session has open is ERR_PROV_IN_USE and the characteristic stays.
 */
MUST_USE err_h sys_ble_link_char_remove(uint16_t char_uuid);

/** @brief Removes the providers of a service's characteristics. Call before sys_ble_service_remove(). */
MUST_USE err_h sys_ble_link_service_remove(uint16_t svc_uuid);

#ifdef __cplusplus
}
#endif
