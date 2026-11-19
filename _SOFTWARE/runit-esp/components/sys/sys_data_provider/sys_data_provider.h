#pragma once

/**
 * @file sys_data_provider.h
 * @brief The board's providers (SYS_DATA_PROVIDER.MD): the ways bytes leave and enter (a UART, a BLE link, the
 * console), each one a stream port of the data matrix. Its control plane is the router only (root key
 * MODULE_ID_PROVIDER 0x0D, contract_provider.json); in C there is only the boot's CREATE of the board's own providers
 * and a provider's state.
 */

#include <stdint.h>

#include "errors.h"
#include "sys_data_provider_errors.h"
#include "sys_data_provider_id.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Provider driver types (u8 wire identifier in CREATE / LIST). */
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

/**
 * @brief A provider's state bits (SYS_DATA_PROVIDER_STATE_*). Any task, no wait.
 * @param[in] id The provider's id
 * @return Its state; 0 when there is no such provider
 */
uint8_t sys_data_provider_state(uint8_t id);

/**
 * @brief CREATE of the board's own provider (SYS_DATA_PROVIDER_STATE_SYSTEM: it can't be removed). Boot code.
 * @param[in] id      The provider ID
 * @param[in] type_id sys_data_provider_type_e
 * @param[in] cfg     The driver's cfg (NULL with `cfg_len` 0 when it has none)
 * @param[in] cfg_len Its size, the type's cfg size
 * @return As CREATE
 */
MUST_USE err_h sys_data_provider_create_system(uint8_t id, uint8_t type_id, const void* cfg, uint32_t cfg_len);

#ifdef __cplusplus
}
#endif
