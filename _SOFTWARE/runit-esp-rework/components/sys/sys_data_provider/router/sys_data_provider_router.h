#pragma once

#include <stdint.h>

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frame under root key MODULE_ID_PROVIDER (0x0D): [dir u8][id u8][op u16][args...], packed, little-endian.
 * `dir` is CMD_DIR_READ or CMD_DIR_WRITE. `id` is a provider ID (0..0xFE) or SYS_DATA_PROVIDER_ID_NONE (0xFF) for LIST.
 * `op` is CONTRACT_PROVIDER_OP_* (0x0D00..0x0D07) or a driver type's own op (0xFF00..0xFFFF).
 */
typedef struct __attribute__((packed)) {
  uint8_t dir;  /**< cmd_dir_e */
  uint8_t id;   /**< Provider ID, or SYS_DATA_PROVIDER_ID_NONE for OP_LIST */
  uint16_t op;  /**< SYS_DATA_PROVIDER_OP(0x0D, n) or SYS_DATA_PROVIDER_OWN_OP(n) */
} sys_data_provider_router_hdr_t;

_Static_assert(sizeof(sys_data_provider_router_hdr_t) == 4, "provider frame header");

/**
 * @brief Router of root key MODULE_ID_PROVIDER (0x0D): remote interface of the provider registry.
 * Self-registers via ROUTER_MODULE_REGISTER(MODULE_ID_PROVIDER, sys_data_provider_router).
 */
ROUTER_MODULE_DECLARE(sys_data_provider_router);

#ifdef __cplusplus
}
#endif
