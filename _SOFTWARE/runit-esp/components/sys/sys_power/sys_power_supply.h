#pragma once

#include "sys_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The POWER_SUPPLY contract (SYS_DEVICE_CONTRACT_POWER_SUPPLY: 0x08xx): a power mux, two switched sources
 * into one load (the LM73100 ideal diodes that feed an H-bridge). At most one source is on; a change switches the
 * old one off first and the new one on after the device's switch delay, so the two are never on together.
 */

//#ref-enum @alias Supply Source
typedef enum {
  SYS_POWER_SUPPLY_OFF = 0,  //@alias Off @description Output unpowered
  SYS_POWER_SUPPLY_A = 1,  //@alias Source A @description Output fed from source A
  SYS_POWER_SUPPLY_B = 2,  //@alias Source B @description Output fed from source B
} sys_power_supply_source_e;

typedef struct __attribute__((packed)) {
  uint8_t source;  /**< sys_power_supply_source_e */
} supply_select_args_t;

/**
 * @brief The supply contract, once: X(NAME, op number, args struct). From it come SUPPLY_OP_<NAME>,
 * supply_args_<NAME>_t, SUPPLY_HANDLER(NAME) and SUPPLY_ENTRY(NAME) (SYS_DEVICE.MD "Contract tables").
 * Numbers are never reused.
 */
// clang-format off
#define SYS_POWER_SUPPLY_CONTRACT(X) \
  X(SELECT, 0x00, supply_select_args_t) \
  X(GET_SOURCE, 0x01, sys_device_none_t)
// clang-format on

#define SYS_POWER_SUPPLY__ID(name, n, args_t) SUPPLY_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_POWER_SUPPLY, n),
#define SYS_POWER_SUPPLY__ARGS(name, n, args_t) typedef args_t supply_args_##name##_t;

enum { SYS_POWER_SUPPLY_CONTRACT(SYS_POWER_SUPPLY__ID) };
SYS_POWER_SUPPLY_CONTRACT(SYS_POWER_SUPPLY__ARGS)

#define SUPPLY_HANDLER(name) SYS_DEVICE_HANDLER(supply_h_##name, supply_args_##name##_t)
#define SUPPLY_ENTRY(name) {SUPPLY_OP_##name, supply_h_##name}

/**
 * @brief Wrappers: the sys_device_call of the op, nothing else.
 * @usage ERR_TRY(sys_power_supply_select(SUPPLY_ID, SYS_POWER_SUPPLY_B));
 */
MUST_USE err_h sys_power_supply_select(uint16_t device, sys_power_supply_source_e source);
MUST_USE err_h sys_power_supply_get_source(uint16_t device, sys_power_supply_source_e* out_source);

#ifdef __cplusplus
}
#endif
