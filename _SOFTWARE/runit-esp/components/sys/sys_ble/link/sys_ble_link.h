#pragma once

#include "errors.h"
#include "sys_ble.h"
#include "sys_data_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates the runIT service (0xFFE0: TX 0xFFE1, RX 0xFFE2, LOGS 0xFFE3, STATUS 0xFFE4), locks it and adds two
 * framed providers: the runIT pair (framed writes on RX are joined and delivered, frames go
 * out on TX) and LOGS (out only). Both are up while a client is connected. Call before sys_ble_start().
 * @param[in] runit_provider The runIT pair's provider id
 * @param[in] logs_provider  The LOGS provider id
 * @return The error of creating the service or the providers
 */
MUST_USE err_h sys_ble_link_init(uint8_t runit_provider, uint8_t logs_provider);

/**
 * @brief Makes a provider of a characteristic created at run time, under the id it is given: unframed, so each write
 * is one message (at most the MTU less the ATT header) and each send one notification. A characteristic that writes
 * with an RX handler (no RX buffer) is an rx provider, one with a TX buffer a tx provider. A framed one, or one with
 * neither, gets none (NULL). Up while a client is connected. Call after sys_ble_char_create().
 * @param[in] svc_uuid Its service.
 * @param[in] cfg      What was created.
 * @param[in] provider Its id.
 * @return NULL; ERR_BLE_NO_MEM (CONFIG_SYS_BLE_USER_PROVIDERS taken), ERR_PROV_EXISTS, ERR_PROV_FULL.
 */
MUST_USE err_h sys_ble_link_char_add(uint16_t svc_uuid, const sys_ble_char_cfg_t* cfg, uint8_t provider);

/**
 * @brief Whether a characteristic has a provider (the runIT pair by its TX), and its id.
 * @param[in]  char_uuid The characteristic's UUID
 * @param[out] provider  The provider id
 * @return true when it has one
 */
bool sys_ble_link_char_provider(uint16_t char_uuid, uint8_t* provider);

/**
 * @brief Removes the provider of a characteristic (none: NULL). Call before sys_ble_char_remove().
 * @param[in] char_uuid The characteristic's UUID
 * @return ERR_PROV_IN_USE when a stream or session has the provider open (the characteristic stays)
 */
MUST_USE err_h sys_ble_link_char_remove(uint16_t char_uuid);

/**
 * @brief Removes the providers of a service's characteristics. Call before sys_ble_service_remove().
 * @param[in] svc_uuid The service's UUID
 * @return ERR_PROV_IN_USE when a stream or session has one open
 */
MUST_USE err_h sys_ble_link_service_remove(uint16_t svc_uuid);

#ifdef __cplusplus
}
#endif
