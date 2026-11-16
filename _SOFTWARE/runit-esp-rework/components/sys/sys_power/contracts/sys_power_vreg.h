#pragma once

#include "sys_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The regulator contract, VREG: 0x05xx, where 0x05 is the contract and xx its op. An adjustable regulator with one
 * output. Voltage and current limit are the wanted settings: a device keeps them while the output is off and applies
 * them when it is enabled. Values the chip can't reach are refused (ERR_DEV_RANGE), never clamped. Writes while
 * suspended: ERR_DEV_SUSPENDED.
 * ENABLE  - output on / off
 * VOLTAGE - output voltage in mV
 * CURRENT - output current limit in mA
 * Events  - OVP, OCP, SCP, READY (VREG_EVENTS below)
 * @usage ERR_TRY(SYS_DEVICE_SET(tps, CONTRACT_VREG, OP_VOLTAGE, .voltage_mV = 9000));
 *        ERR_TRY(SYS_DEVICE_SET(tps, CONTRACT_VREG, OP_ENABLE, .enable = 1));
 */

/** @brief ENABLE args */
typedef struct __attribute__((packed)) {
  uint8_t enable;  /**< 1 = Output enabled, 0 = Output disabled */
} vreg_enable_args_t;

/** @brief VOLTAGE args */
typedef struct __attribute__((packed)) {
  uint32_t voltage_mV;  /**< Output voltage in mV */
} vreg_voltage_args_t;

/** @brief CURRENT args */
typedef struct __attribute__((packed)) {
  uint32_t current_mA;  /**< Output current limit in mA */
} vreg_current_args_t;

/**
 * @brief The contract, once: X(P, OP_NAME, op number, key_t, value_t, flags). Sides: ENABLE W, VOLTAGE W, CURRENT W.
 */
#define CONTRACT_VREG_OPERATIONS(X, P)                                \
  X(P, OP_ENABLE,  0x00, cmd_none_t, vreg_enable_args_t,  0) \
  X(P, OP_VOLTAGE, 0x01, cmd_none_t, vreg_voltage_args_t, 0) \
  X(P, OP_CURRENT, 0x02, cmd_none_t, vreg_current_args_t, 0)

MODULE_ID_DECLARE(VREG, 0x05)  //@alias Voltage regulator contract @description Programmable voltage regulator contract and module ID
SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_VREG, VREG, 0x05)

/* Events of the contract: source = the device, channel 0. The faults matter to the user (NOTIFY), with the output's
 * setting at the time; READY is code flow (a program waiting for its rail after ENABLE / a higher VOLTAGE, which
 * return at once), with the voltage reached. */
#define VREG_EVENTS(X, P)                              \
  X(P, OVP,   0x01, SYS_EVENT_NOTIFY, { int32_t mV; }) \
  X(P, OCP,   0x02, SYS_EVENT_NOTIFY, { int32_t mA; }) \
  X(P, SCP,   0x03, SYS_EVENT_NOTIFY, {})              \
  X(P, READY, 0x04, 0,                { int32_t mV; })

SYS_EVENTS_DECLARE(VREG, DEVICE)

#ifdef __cplusplus
}
#endif
