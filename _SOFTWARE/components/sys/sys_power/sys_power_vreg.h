#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sys_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The POWER_VREG contract (SYS_DEVICE_CONTRACT_POWER_VREG: 0x05xx): an
 * adjustable regulator with one output. Voltage and current limit are the
 * wanted settings: a device keeps them while the output is off and applies
 * them when it is enabled. Values the chip can't reach are refused
 * (ERR_DEV_RANGE), never clamped. Writes while suspended: ERR_DEV_SUSPENDED.
 */

/* Events of the vreg contract (sys_event.h "Events tables"); channel 0. The
 * faults matter to the user (NOTIFY); READY is code flow (a program waiting for
 * its rail after SET_ENABLE / a higher SET_VOLTAGE, which return at once). */
// clang-format off
#define SYS_POWER_VREG_EVENTS(X)                                              \
  X(OVP, 0x00, SYS_EVENT_NOTIFY)  /* over-voltage; value = wanted mV */       \
  X(OCP, 0x01, SYS_EVENT_NOTIFY)  /* over-current; value = current limit mA */ \
  X(SCP, 0x02, SYS_EVENT_NOTIFY)  /* short circuit; value 0 */                \
  X(READY, 0x03, 0)               /* the output settled at the wanted voltage; value = mV */
// clang-format on

#define SYS_POWER_VREG__EV(name, n, flags) \
  VREG_EV_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_POWER_VREG, n), VREG_EV_##name##_FLAGS = (flags),

enum { SYS_POWER_VREG_EVENTS(SYS_POWER_VREG__EV) };

typedef struct __attribute__((packed)) {
  uint8_t enable;  // 0 / 1
} vreg_enable_args_t;

typedef struct __attribute__((packed)) {
  uint32_t voltage_mV;
} vreg_voltage_args_t;

typedef struct __attribute__((packed)) {
  uint32_t current_mA;
} vreg_current_args_t;

/**
 * @brief The vreg contract, once: X(NAME, op number, args struct). From it come
 * VREG_OP_<NAME>, vreg_args_<NAME>_t, VREG_HANDLER(NAME) and VREG_ENTRY(NAME)
 * (SYS_DEVICE.MD "Contract tables"). Numbers are never reused.
 */
// clang-format off
#define SYS_POWER_VREG_CONTRACT(X)                                                  \
  X(SET_ENABLE,  0x00, vreg_enable_args_t)   /* output on / off */                 \
  X(SET_VOLTAGE, 0x01, vreg_voltage_args_t)  /* wanted output voltage */           \
  X(SET_CURRENT, 0x02, vreg_current_args_t)  /* wanted output current limit */
// clang-format on

#define SYS_POWER_VREG__ID(name, n, args_t) VREG_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_POWER_VREG, n),
#define SYS_POWER_VREG__ARGS(name, n, args_t) typedef args_t vreg_args_##name##_t;

enum { SYS_POWER_VREG_CONTRACT(SYS_POWER_VREG__ID) };
SYS_POWER_VREG_CONTRACT(SYS_POWER_VREG__ARGS)

#define VREG_HANDLER(name) SYS_DEVICE_HANDLER(vreg_h_##name, vreg_args_##name##_t)
#define VREG_ENTRY(name) {VREG_OP_##name, vreg_h_##name}

/**
 * @brief Wrappers: the sys_device_call of the op, nothing else.
 * @usage ERR_TRY(sys_power_vreg_set_voltage(TPS_A_ID, 9000)); ERR_TRY(sys_power_vreg_set_enable(TPS_A_ID, true));
 */
MUST_USE err_h sys_power_vreg_set_enable(uint16_t device, bool enable);
MUST_USE err_h sys_power_vreg_set_voltage(uint16_t device, uint32_t voltage_mV);
MUST_USE err_h sys_power_vreg_set_current(uint16_t device, uint32_t current_mA);

#ifdef __cplusplus
}
#endif
