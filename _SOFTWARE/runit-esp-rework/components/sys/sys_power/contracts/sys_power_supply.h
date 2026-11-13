#pragma once

#include "sys_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The power mux contract, SUPPLY: 0x08xx, where 0x08 is the contract and xx its op. Two switched sources into one load
 * (the LM73100 ideal diodes that feed an H-bridge). At most one source is on; a change switches the old one off first
 * and the new one on after the device's switch delay, so the two are never on together.
 * SOURCE - which source feeds the load (sys_power_supply_source_e), set and read
 * No events.
 * @usage ERR_TRY(SYS_DEVICE_SET(mux, SUPPLY, SOURCE, .source = SYS_POWER_SUPPLY_B));
 */

//#ref-enum @alias Supply Source
typedef enum {
  SYS_POWER_SUPPLY_OFF = 0,  //@alias Off @description Output unpowered
  SYS_POWER_SUPPLY_A = 1,    //@alias Source A @description Output fed from source A
  SYS_POWER_SUPPLY_B = 2,    //@alias Source B @description Output fed from source B
} sys_power_supply_source_e;

/** @brief SOURCE args and answer */
typedef struct __attribute__((packed)) {
  uint8_t source;  /**< sys_power_supply_source_e */
} supply_select_args_t;

/**
 * @brief The contract, once: X(P, NAME, op number, key_t, value_t, flags). Sides: SOURCE RW.
 */
#define SUPPLY_OPERATIONS(X, P)                           \
  X(P, SOURCE, 0x00, cmd_none_t, supply_select_args_t, 0)

MODULE_ID_DECLARE(SUPPLY, 0x08)
SYS_DEVICE_CONTRACT_DECLARE(SUPPLY, 0x08)

#ifdef __cplusplus
}
#endif
