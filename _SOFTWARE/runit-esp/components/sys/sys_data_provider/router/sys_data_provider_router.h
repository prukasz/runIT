#pragma once

#include "router.h"
#include "sys_data_provider.h"

/**
 * @file sys_data_provider_router.h
 * @brief Provider router (root key ROUTER_KEY_PROVIDER = 0x0D): the remote interface of SYS_DATA_PROVIDER.MD
 * "Remote". Only components/app/runit includes it, to register it. A provider is created by its driver, from the
 * driver's own config (a BLE characteristic, a UART port).
 * @usage ERR_TRY(router_register(ROUTER_KEY_PROVIDER, sys_data_provider_router));
 *
 *   code command  side   request / answer
 *   0x01 REMOVE   write  sys_data_provider_id_t
 *   0x02 LIST     read   -> u8 count, then sys_data_provider_info_t per provider
 *   0x03 SEND     write  sys_data_provider_id_t, then the frame; an open provider is its user's (ERR_PROV_IN_USE)
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_DATA_PROVIDER_OP_REMOVE 0x01u
#define SYS_DATA_PROVIDER_OP_LIST   0x02u
#define SYS_DATA_PROVIDER_OP_SEND   0x03u

/** @brief A provider as LIST sends it. */
typedef struct __attribute__((packed)) {
  sys_data_provider_id_t id;
  uint16_t max_frame;  /**< Now; 0 = down */
  uint8_t state;       /**< SYS_DATA_PROVIDER_STATE_*: the provider's own, as is */
} sys_data_provider_info_t;

ROUTER_MODULE_DECLARE(sys_data_provider_router);

#ifdef __cplusplus
}
#endif
