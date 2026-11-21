#pragma once

#include <stdint.h>

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frame under root key MODULE_ID_PROVIDER (0x2B): [dir u8][id u8][op u16][args...], packed, little-endian.
 * `dir` is CMD_DIR_READ or CMD_DIR_WRITE. `id` is a provider ID; LIST takes any.
 * `op` is the contract's (0x2Bxx, contract_provider.json) or a driver type's own (SYS_DATA_PROVIDER_OWN_OP, 0xFFxx).
 */
typedef struct __attribute__((packed)) {
  uint8_t dir;  /**< cmd_dir_e */
  uint8_t id;   /**< Provider ID; LIST takes any */
  uint16_t op;  /**< 0x2Bxx or SYS_DATA_PROVIDER_OWN_OP(n) */
} sys_data_provider_router_hdr_t;

_Static_assert(sizeof(sys_data_provider_router_hdr_t) == 4, "provider frame header");

/**
 * @brief Router of root key MODULE_ID_PROVIDER (0x2B): remote interface of the provider registry.
 * Self-registers via ROUTER_MODULE_REGISTER(MODULE_ID_PROVIDER, sys_data_provider_router).
 */
ROUTER_MODULE_DECLARE(sys_data_provider_router);

#ifdef __cplusplus
}
#endif
