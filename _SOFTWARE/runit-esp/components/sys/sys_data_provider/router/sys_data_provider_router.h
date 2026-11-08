#pragma once

#include "router.h"
#include "sys_data_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_PROVIDER = 0x0D (SYS_DATA_PROVIDER.MD "Remote"), each above the frame it uses (packed,
 * little-endian; contract_provider.json names them), with its sides. Never renumber. A provider is created by its
 * driver's own command, from its own config (a BLE characteristic: ROUTER_KEY_BLE; a UART port: ROUTER_KEY_UART): no op
 * here creates one. */

#define SYS_DATA_PROVIDER_OP_REMOVE          0x01u  // W: sys_data_provider_ref_t
#define SYS_DATA_PROVIDER_OP_BIND_COMMANDS   0x04u  // W: sys_data_provider_ref_t, its messages are commands for this board, no header
#define SYS_DATA_PROVIDER_OP_UNBIND_COMMANDS 0x05u  // W: sys_data_provider_ref_t
#define SYS_DATA_PROVIDER_OP_SEND            0x03u  // W: sys_data_provider_ref_t + the frame
typedef struct __attribute__((packed)) {
  uint8_t provider;  /**< Its id */
} sys_data_provider_ref_t;

#define SYS_DATA_PROVIDER_OP_LIST 0x02u  // R: -> u8 count, then sys_data_provider_info_t per provider
/** @brief A provider as LIST sends it. */
typedef struct __attribute__((packed)) {
  uint8_t provider;  /**< Its id */
  uint8_t link;      /**< sys_data_provider_link_e */
  uint8_t state;     /**< SYS_DATA_PROVIDER_STATE_*: the provider's own, as is */
} sys_data_provider_info_t;

/**
 * @brief The data providers, under ROUTER_KEY_PROVIDER. Only components/app/runit includes it, to register it.
 * Ops and layouts: SYS_DATA_PROVIDER.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_PROVIDER, sys_data_provider_router));
 */
ROUTER_MODULE_DECLARE(sys_data_provider_router);

#ifdef __cplusplus
}
#endif
