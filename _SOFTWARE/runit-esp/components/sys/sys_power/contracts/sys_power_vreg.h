#pragma once

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
#define SYS_POWER_VREG_EVENTS(X)   \
  X(OVP,   0x00, SYS_EVENT_NOTIFY) \
  X(OCP,   0x01, SYS_EVENT_NOTIFY) \
  X(SCP,   0x02, SYS_EVENT_NOTIFY) \
  X(READY, 0x03, 0)

#define SYS_POWER_VREG__EV(name, n, flags) \
  VREG_EV_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_POWER_VREG, n), VREG_EV_##name##_FLAGS = (flags),

enum { SYS_POWER_VREG_EVENTS(SYS_POWER_VREG__EV) };

typedef struct __attribute__((packed)) {
  uint8_t enable;       /**< 1 = Output enabled, 0 = Output disabled */
} vreg_enable_args_t;

typedef struct __attribute__((packed)) {
  uint32_t voltage_mV;  /**< Output voltage in mV */
} vreg_voltage_args_t;

typedef struct __attribute__((packed)) {
  uint32_t current_mA;  /**< Output current limit in mA */
} vreg_current_args_t;

/**
 * @brief The contract, once: X(NAME, op number, key_t, value_t). The read takes key_t and answers value_t, the write
 * takes value_t (the mirror). From it come VREG_OP_<NAME>, vreg_key_<NAME>_t, vreg_value_<NAME>_t, VREG_READ(NAME) /
 * VREG_WRITE(NAME) and VREG_ENTRY(NAME), _ENTRY_R, _ENTRY_W (SYS_DEVICE.MD "Contract tables"). Sides: ENABLE W, VOLTAGE
 * W, CURRENT W. Numbers are never reused.
 */
#define SYS_POWER_VREG_CONTRACT(X)                         \
  X(ENABLE,  0x00, sys_device_none_t, vreg_enable_args_t)  \
  X(VOLTAGE, 0x01, sys_device_none_t, vreg_voltage_args_t) \
  X(CURRENT, 0x02, sys_device_none_t, vreg_current_args_t)

#define SYS_POWER_VREG__ID(name, n, key_t, value_t) VREG_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_POWER_VREG, n),
#define SYS_POWER_VREG__TYPES(name, n, key_t, value_t) \
  typedef key_t vreg_key_##name##_t;                   \
  typedef value_t vreg_value_##name##_t;

enum { SYS_POWER_VREG_CONTRACT(SYS_POWER_VREG__ID) };
SYS_POWER_VREG_CONTRACT(SYS_POWER_VREG__TYPES)

#define VREG_READ(name)    SYS_DEVICE_READ(vreg, name)
#define VREG_WRITE(name)   SYS_DEVICE_WRITE(vreg, name)
#define VREG_ENTRY(name)   SYS_DEVICE_ENTRY(VREG_OP_##name, vreg, name)
#define VREG_ENTRY_R(name) SYS_DEVICE_ENTRY_R(VREG_OP_##name, vreg, name)
#define VREG_ENTRY_W(name) SYS_DEVICE_ENTRY_W(VREG_OP_##name, vreg, name)

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
