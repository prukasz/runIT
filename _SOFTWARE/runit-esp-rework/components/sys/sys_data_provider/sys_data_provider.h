#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "errors.h"
#include "sys_data_provider_config.h"
#include "sys_data_provider_errors.h"
#include "utils.h"

/**
 * @file sys_data_provider.h
 * @brief Transport provider registry and centralized router interface (MODULE_ID_PROVIDER = 0x0D).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_DATA_PROVIDER_ID_NONE 0xFFu  /**< Never a provider; OP_LIST targets it */

#define SYS_DATA_PROVIDER_OP(contract, n) ((uint16_t)(((contract) << 8) | (n)))
#define SYS_DATA_PROVIDER_OWN_OP(n)       SYS_DATA_PROVIDER_OP(0xFFu, (n))
#define SYS_DATA_PROVIDER_OP_LOCAL        (1u << 0)  /**< C-only op: blocked from router and omitted from OP_OPS */

#define SYS_DATA_PROVIDER_CREATE_HDR    2u     /**< [type u8][flags u8], followed by packed driver cfg */
#define SYS_DATA_PROVIDER_CREATE_SYSTEM 0x01u  /**< Board's own provider: cannot be removed (ERR_PROV_SYSTEM) */

typedef enum {
  CONTRACT_PROVIDER_OP_CREATE          = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x00),
  CONTRACT_PROVIDER_OP_REMOVE          = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x01),
  CONTRACT_PROVIDER_OP_LIST            = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x02),
  CONTRACT_PROVIDER_OP_SEND            = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x03),
  CONTRACT_PROVIDER_OP_BIND_COMMANDS   = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x04),
  CONTRACT_PROVIDER_OP_UNBIND_COMMANDS = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x05),
  CONTRACT_PROVIDER_OP_INFO            = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x06),
  CONTRACT_PROVIDER_OP_OPS             = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x07),
} contract_provider_op_e;

_Static_assert(MODULE_ID_PROVIDER == 0x0D, "MODULE_ID_PROVIDER must stay 0x0D");

/** @brief OP_INFO answer (2 B). */
typedef struct __attribute__((packed)) {
  uint8_t type;   /**< sys_data_provider_type_e */
  uint8_t state;  /**< SYS_DATA_PROVIDER_STATE_* */
} sys_data_provider_info_t;

/** @brief One row in OP_LIST answer ([u8 count][rows...], 3 B each). */
typedef struct __attribute__((packed)) {
  uint8_t provider;  /**< Provider ID */
  uint8_t type;      /**< sys_data_provider_type_e */
  uint8_t state;     /**< SYS_DATA_PROVIDER_STATE_* */
} sys_data_provider_entry_t;

/* ----------------------------------------------------------------------------
 * Control calls
 * ------------------------------------------------------------------------- */

MUST_USE err_h sys_data_provider_call(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out);
MUST_USE err_h sys_data_provider_write(uint8_t id, uint16_t op, const void* args, uint32_t args_len);
MUST_USE err_h sys_data_provider_read(uint8_t id, uint16_t op, const void* args, uint32_t args_len, void* out,
                                      uint32_t out_size);

/* ----------------------------------------------------------------------------
 * Data plane
 * ------------------------------------------------------------------------- */

typedef void (*sys_data_provider_rx_f)(void* ctx, uint8_t id, const uint8_t* data, uint32_t len);

MUST_USE err_h sys_data_provider_open(uint8_t id, sys_data_provider_rx_f rx, void* ctx);
MUST_USE err_h sys_data_provider_close(uint8_t id);
MUST_USE err_h sys_data_provider_send(uint8_t id, cmd_in_t frame, uint8_t flags);
uint8_t sys_data_provider_state(uint8_t id);

#ifdef __cplusplus
}
#endif
