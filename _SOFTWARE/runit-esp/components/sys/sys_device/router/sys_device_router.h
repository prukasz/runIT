#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The frame under ROUTER_KEY_DEVICE (SYS_DEVICE.MD, contract_device.json): [dir u8][id u16][op u16][args], packed,
 * little-endian. dir is CMD_DIR_READ or CMD_DIR_WRITE; the op and its args are the device's own (its contract_*.json),
 * so no op is listed here. */
typedef struct __attribute__((packed)) {
  uint8_t dir;
  uint16_t id;  /**< A device of this board, or SYS_DEVICE_ID_NONE for every one (sweep ops) */
  uint16_t op;
} sys_device_router_hdr_t;

/**
 * @brief Router of ROUTER_KEY_DEVICE: [dir][id u16 LE][op u16 LE][args] (SYS_DEVICE.MD).
 * The remote interface of the registry, nothing else: the registry
 * (sys_device.h) and everything built on it don't include the router, only the
 * app that registers this one.
 * @usage ERR_TRY(router_register(ROUTER_KEY_DEVICE, sys_device_router));
 */
ROUTER_MODULE_DECLARE(sys_device_router);

#ifdef __cplusplus
}
#endif
