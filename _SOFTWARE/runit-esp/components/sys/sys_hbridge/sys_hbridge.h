#pragma once

#include "sys_device.h"
#include "sys_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The HBRIDGE contract (SYS_DEVICE_CONTRACT_HBRIDGE: 0x09xx): a motor driver with channels. A channel is a full
 * bridge (a motor, both directions) or a half bridge (a solenoid, one direction), as the driver is configured. Chip
 * specifics (current limit, fault latch, ...) are the driver's own ops.
 *
 * Drive is a signed number in PWM steps: -4095 (full reverse) .. +4095 (full forward) for a full bridge, 0 .. 4095 for
 * a half bridge: the 12 bits of IO_PWM_DUTY_MAX (SYS_IO.MD), so the driver loses no resolution between the contract and
 * the PWM. A full bridge brakes below 20 (0.5 %).
 */

//#ref-enum @alias H-Bridge Fault
typedef enum sys_hbridge_fault_reason_e {
  SYS_HBRIDGE_FAULT_NONE = 0,  //@alias None @description No fault
  SYS_HBRIDGE_FAULT_OVERCURRENT = 1,  //@alias Overcurrent @description Overcurrent condition on channel
  SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO = 2,  //@alias Overheat or Low Supply @description Thermal shutdown or undervoltage lockout
  SYS_HBRIDGE_FAULT_EXTERNAL = 3,  //@alias External @description External fault input tripped
} sys_hbridge_fault_reason_e;

#define SYS_HBRIDGE_DRIVE_MAX 4095  // = IO_PWM_DUTY_MAX: 12 bits

typedef struct __attribute__((packed)) {
  uint8_t channel;  /**< Motor channel index */
} hbridge_channel_args_t;

typedef struct __attribute__((packed)) {
  uint8_t channel;  /**< Motor channel index */
  int16_t drive;    /**< PWM drive: -4095 (reverse) to +4095 (forward) */
} hbridge_drive_args_t;

/**
 * @brief The contract, once: X(P, NAME, op number, key_t, value_t). The read takes key_t and answers value_t, the write
 * takes value_t (the mirror). SYS_DEVICE_CONTRACT_DECLARE gives HBRIDGE_OP_<NAME>, HBRIDGE_key_<NAME>_t and
 * HBRIDGE_value_<NAME>_t; a driver writes OP_READ(HBRIDGE, NAME) / OP_WRITE(HBRIDGE, NAME) and lists OP_ENTRY(HBRIDGE,
 * NAME), OP_ENTRY_R, OP_ENTRY_W (SYS_DEVICE.MD "Contract tables"). Sides: DRIVE W, BRAKE W, COAST W, CURRENT R. Numbers
 * are never reused.
 */
#define SYS_HBRIDGE_CONTRACT(X, P)                                    \
  X(P, DRIVE,   0x00, sys_device_none_t,      hbridge_drive_args_t)   \
  X(P, BRAKE,   0x01, sys_device_none_t,      hbridge_channel_args_t) \
  X(P, COAST,   0x02, sys_device_none_t,      hbridge_channel_args_t) \
  X(P, CURRENT, 0x03, hbridge_channel_args_t, int32_t)

SYS_DEVICE_CONTRACT_DECLARE(SYS_HBRIDGE_CONTRACT, HBRIDGE, SYS_DEVICE_CONTRACT_HBRIDGE)

/* Events of the contract (sys_event.h "Events tables"): a channel fault, published by the driver; channel = the
 * channel, value = its current in mA. A fault is something the user must see: NOTIFY. */
#define SYS_HBRIDGE_EVENTS(X) \
  X(OVERCURRENT, 0x00, SYS_EVENT_NOTIFY) \
  X(THERMAL_OR_UVLO, 0x01, SYS_EVENT_NOTIFY) \
  X(EXTERNAL, 0x02, SYS_EVENT_NOTIFY)

#define SYS_HBRIDGE__EV(name, n, flags) \
  HBRIDGE_EV_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_HBRIDGE, n), HBRIDGE_EV_##name##_FLAGS = (flags),

enum { SYS_HBRIDGE_EVENTS(SYS_HBRIDGE__EV) };

/**
 * @brief Wrappers: the sys_device_call of the op, nothing else.
 * @usage ERR_TRY(sys_hbridge_set_drive(BRIDGE_ID, 0, -2048));
 */
MUST_USE err_h sys_hbridge_set_drive(uint16_t device, uint8_t channel, int16_t drive);
MUST_USE err_h sys_hbridge_brake(uint16_t device, uint8_t channel);
MUST_USE err_h sys_hbridge_coast(uint16_t device, uint8_t channel);
MUST_USE err_h sys_hbridge_get_current_mA(uint16_t device, uint8_t channel, int32_t* out_mA);

#ifdef __cplusplus
}
#endif
