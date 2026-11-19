#pragma once

#include <stdint.h>

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The frame under the root key MODULE_ID_SYS_DEVICE: [dir u8][id u8][op u16][args], packed, little-endian. `dir` is
 * CMD_DIR_READ or CMD_DIR_WRITE; the op and its args are the device's own (its contract_*.json), so no op is listed
 * here. The same frame comes from the app and from another board (sys_remote, with the device's ID on this board).
 */
typedef struct __attribute__((packed)) {
  uint8_t dir;  /**< cmd_dir_e */
  uint8_t id;   /**< A device of this board */
  uint16_t op;  /**< SYS_DEVICE_OP(contract, n) */
} sys_device_router_hdr_t;

_Static_assert(sizeof(sys_device_router_hdr_t) == 4, "device frame header");

/**
 * @brief Router of the root key MODULE_ID_SYS_DEVICE: the remote interface of the registry, nothing else. It registers itself
 * (ROUTER_MODULE_REGISTER), so no component names it; the registry (sys_device.h) and everything built on it don't
 * include the router. The header is for whoever builds the frame (sys_remote, sys_actions).
 */
ROUTER_MODULE_DECLARE(sys_device_router);

#ifdef __cplusplus
}
#endif
