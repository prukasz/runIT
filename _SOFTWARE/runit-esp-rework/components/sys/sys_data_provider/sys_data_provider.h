#pragma once

/**
 * @file sys_data_provider.h
 * @brief The board's providers (SYS_DATA_PROVIDER.MD): the ways bytes leave and enter (a UART, a BLE link, the
 * console), each one a stream port of the data matrix. Its data plane is the port only: what comes in on a line goes
 * out of the provider's port, what the port takes is sent on the line. Its control plane is the router only (root key
 * MODULE_ID_PROVIDER 0x0D: CREATE, REMOVE, LIST, INFO, OPS and a driver's own ops). In C there is the boot's CREATE of
 * the board's own providers and the hooks that make every provider a port.
 */

#include <stdint.h>

#include "cmd.h"
#include "data_stream_port.h"
#include "errors.h"
#include "module_id.h"
#include "sys_data_provider_errors.h"

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

/* Provider state bits (filled by the driver inside create() and sys_data_provider_set_up()). */
//#ref-defines prov_state @alias Provider state @prefix SYS_DATA_PROVIDER_STATE_
#define SYS_DATA_PROVIDER_STATE_SYSTEM (1u << 0)  //@alias System @description The board's own: not removed
#define SYS_DATA_PROVIDER_STATE_WIRED  (1u << 3)  //@alias Wired @description Connection realised by cable - low latency
#define SYS_DATA_PROVIDER_STATE_UP     (1u << 4)  //@alias Up @description It delivers frames now
#define SYS_DATA_PROVIDER_STATE_FRAMED (1u << 5)  //@alias Framed @description Its driver carries a whole CMD_FRAME_MAX frame; else raw

#define SYS_DATA_PROVIDER_ID_NONE 0xFFu  /**< Never a provider; OP_LIST targets it */

/** @brief An op code: the contract (MODULE_ID_PROVIDER, or 0xFF for a driver's own) in the high byte. */
#define SYS_DATA_PROVIDER_OP(contract, n) ((uint16_t)(((contract) << 8) | (n)))
/** @brief A driver's own op code (its type's ops table). */
#define SYS_DATA_PROVIDER_OWN_OP(n)       SYS_DATA_PROVIDER_OP(0xFFu, (n))

#define SYS_DATA_PROVIDER_CREATE_HDR    2u     /**< [type u8][flags u8], followed by packed driver cfg */
#define SYS_DATA_PROVIDER_CREATE_SYSTEM 0x01u  /**< Board's own provider: cannot be removed (ERR_PROV_SYSTEM) */

/** @brief The provider contract's ops (router root key MODULE_ID_PROVIDER, contract_provider.json). Never renumber. */
typedef enum {
  CONTRACT_PROVIDER_OP_CREATE = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x00),
  CONTRACT_PROVIDER_OP_REMOVE = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x01),
  CONTRACT_PROVIDER_OP_LIST   = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x02),
  CONTRACT_PROVIDER_OP_INFO   = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x04),
  CONTRACT_PROVIDER_OP_OPS    = SYS_DATA_PROVIDER_OP(MODULE_ID_PROVIDER, 0x05),
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

/**
 * @brief What is told of a provider's stream port: the port embedded in the provider, named
 * DATA_STREAM_PORT(DATA_STREAM_PORT_PROVIDER, id); its input sends on the line with the packet's flags and
 * CMD_SEND_NOWAIT (what the line does not take is lost). The switch's data_stream_register / data_stream_unregister
 * have this shape: the app hands them over, so neither names the other.
 * @param port  The provider's port; lives as long as the provider.
 * @return NULL; an error is reported by the registry
 */
typedef err_h (*sys_data_provider_port_hook_f)(data_stream_port_t* port);

/**
 * @brief Who hears of the providers' stream ports: the app sets the switch's register and unregister
 * (`sys_data_provider_set_port_hooks(data_stream_register, data_stream_unregister)`). `added` is called at once for
 * every provider already made, then for each one CREATE makes; `removed` for each one REMOVE takes, first, before its
 * driver is removed. Boot, once.
 * @param added    Called with a new provider's port, may be NULL.
 * @param removed  Called with a provider's port before it goes, may be NULL.
 */
void sys_data_provider_set_port_hooks(sys_data_provider_port_hook_f added, sys_data_provider_port_hook_f removed);

#ifdef __cplusplus
}
#endif
