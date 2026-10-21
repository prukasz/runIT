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
  SYS_POWER_SUPPLY_A = 1,    //@alias Source A @description Output fed from source A
  SYS_POWER_SUPPLY_B = 2,    //@alias Source B @description Output fed from source B
} sys_power_supply_source_e;

typedef struct __attribute__((packed)) {
  uint8_t source;  /**< sys_power_supply_source_e */
} supply_select_args_t;

/**
 * @brief The contract, once: X(P, NAME, op number, key_t, value_t). The read takes key_t and answers value_t, the write
 * takes value_t (the mirror). SYS_DEVICE_CONTRACT_DECLARE gives SUPPLY_OP_<NAME>, SUPPLY_key_<NAME>_t and
 * SUPPLY_value_<NAME>_t; a driver writes OP_READ(SUPPLY, NAME) / OP_WRITE(SUPPLY, NAME) and lists OP_ENTRY(SUPPLY,
 * NAME), OP_ENTRY_R, OP_ENTRY_W (SYS_DEVICE.MD "Contract tables"). Sides: SOURCE RW. Numbers are never reused.
 */
#define SYS_POWER_SUPPLY_CONTRACT(X, P) \
  X(P, SOURCE, 0x00, sys_device_none_t, supply_select_args_t)

#define SUPPLY_CONTRACT SYS_DEVICE_CONTRACT_POWER_SUPPLY
SYS_DEVICE_CONTRACT_DECLARE(SYS_POWER_SUPPLY_CONTRACT, SUPPLY)

/**
 * @brief Wrappers: the sys_device_call of the op, nothing else.
 * @usage ERR_TRY(sys_power_supply_select(SUPPLY_ID, SYS_POWER_SUPPLY_B));
 */
MUST_USE err_h sys_power_supply_select(uint16_t device, sys_power_supply_source_e source);
MUST_USE err_h sys_power_supply_get_source(uint16_t device, sys_power_supply_source_e* out_source);

#ifdef __cplusplus
}
#endif
