#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "errors.h"
#include "module_id.h"
#include "sys_data_provider_errors.h"
#include "utils.h"

/**
 * @file sys_data_provider.h
 * @brief Transport provider registry and centralized router interface (MODULE_ID_PROVIDER = 0x0D).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Provider driver types (u8 wire identifier in CREATE / INFO / LIST). */
//#ref-enum @alias Provider Type
typedef enum {
  SYS_DATA_PROVIDER_TYPE_UART    = 1,  //@alias UART @description UART bridge provider
  SYS_DATA_PROVIDER_TYPE_BLE     = 2,  //@alias Bluetooth LE @description BLE link provider
  SYS_DATA_PROVIDER_TYPE_MQTT    = 3,  //@alias MQTT @description MQTT topic provider
  SYS_DATA_PROVIDER_TYPE_CONSOLE = 4,  //@alias Console @description Board UART/USB-JTAG console
} sys_data_provider_type_e;

/* Provider state bits (filled by the driver inside create() and sys_data_provider_set_up(); OPEN is the registry's). */
//#ref-defines prov_state @alias Provider state @prefix SYS_DATA_PROVIDER_STATE_
#define SYS_DATA_PROVIDER_STATE_SYSTEM (1u << 0)  //@alias System @description The board's own: not removed
#define SYS_DATA_PROVIDER_STATE_OPEN   (1u << 1)  //@alias Open @description Its rx hook has an owner
#define SYS_DATA_PROVIDER_STATE_WIRED  (1u << 3)  //@alias Wired @description Connection realised by cable - low latency
#define SYS_DATA_PROVIDER_STATE_UP     (1u << 4)  //@alias Up @description It delivers frames now
#define SYS_DATA_PROVIDER_STATE_FRAMED (1u << 5)  //@alias Framed @description Its driver carries a whole CMD_FRAME_MAX frame; else raw

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
  CONTRACT_PROVIDER_OP_INFO            = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x04),
  CONTRACT_PROVIDER_OP_OPS             = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x05),
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

/** @brief The largest driver cfg sys_data_provider_create_system takes. */
#define SYS_DATA_PROVIDER_CREATE_CFG_MAX 32u

/**
 * @brief CREATE of the board's own provider (SYS_DATA_PROVIDER_CREATE_SYSTEM: it can't be removed): the frame
 * [type u8][flags u8][cfg] built here, so the caller names the type and the driver's cfg only. Boot code.
 * @param[in] id      The provider ID
 * @param[in] type_id sys_data_provider_type_e
 * @param[in] cfg     The driver's cfg (NULL with `cfg_len` 0 when it has none)
 * @param[in] cfg_len Its size, at most SYS_DATA_PROVIDER_CREATE_CFG_MAX
 * @return As CREATE; ERR_CMD_BAD_LEN when `cfg_len` is over the limit
 */
MUST_USE err_h sys_data_provider_create_system(uint8_t id, uint8_t type_id, const void* cfg, uint32_t cfg_len);
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
