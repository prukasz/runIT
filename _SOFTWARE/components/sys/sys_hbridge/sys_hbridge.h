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
  SYS_HBRIDGE_FAULT_OVERCURRENT = 1,  //@alias Overcurrent @description The channel drew more than its current limit
  SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO = 2,  //@alias Overheat or Low Supply @description The driver chip overheated or its supply dropped too low; every channel stops
  SYS_HBRIDGE_FAULT_EXTERNAL = 3,  //@alias External @description An external fault input was triggered
} sys_hbridge_fault_reason_e;

#define SYS_HBRIDGE_DRIVE_MAX 4095  // = IO_PWM_DUTY_MAX: 12 bits

typedef struct __attribute__((packed)) {
  uint8_t channel;  //@alias Channel @description The motor channel @id pin
} hbridge_channel_args_t;

typedef struct __attribute__((packed)) {
  uint8_t channel;  //@alias Channel @description The motor channel @id pin
  int16_t drive;  //@alias Drive @description PWM steps: -4095 full reverse to 4095 full forward (a half bridge 0 to 4095) @min -4095 @max 4095
} hbridge_drive_args_t;

/**
 * @brief The contract, once: X(NAME, op number, args struct). From it come HBRIDGE_OP_<NAME>,
 * hbridge_args_<NAME>_t, HBRIDGE_HANDLER(NAME) and HBRIDGE_ENTRY(NAME) (SYS_DEVICE.MD "Contract tables").
 * Numbers are never reused.
 */
// clang-format off
#define SYS_HBRIDGE_CONTRACT(X) \
  X(SET_DRIVE, 0x00, hbridge_drive_args_t)  /* @alias Drive @description Drives a motor channel; out of range is an error. */ \
  X(BRAKE, 0x01, hbridge_channel_args_t)  /* @alias Brake @description Shorts the motor terminals. */ \
  X(COAST, 0x02, hbridge_channel_args_t)  /* @alias Coast @description Releases the motor: the outputs are off. */ \
  X(GET_CURRENT, 0x03, hbridge_channel_args_t)  /* @alias Read current @description Reads the current of a channel. @returns i32 @returns-alias Current @unit mA */
// clang-format on

#define SYS_HBRIDGE__ID(name, n, args_t) HBRIDGE_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_HBRIDGE, n),
#define SYS_HBRIDGE__ARGS(name, n, args_t) typedef args_t hbridge_args_##name##_t;

enum { SYS_HBRIDGE_CONTRACT(SYS_HBRIDGE__ID) };
SYS_HBRIDGE_CONTRACT(SYS_HBRIDGE__ARGS)

#define HBRIDGE_HANDLER(name) SYS_DEVICE_HANDLER(hbridge_h_##name, hbridge_args_##name##_t)
#define HBRIDGE_ENTRY(name) {HBRIDGE_OP_##name, hbridge_h_##name}

/* Events of the contract (sys_event.h "Events tables"): a channel fault, published by the driver; channel = the
 * channel, value = its current in mA. A fault is something the user must see: NOTIFY. */
// clang-format off
#define SYS_HBRIDGE_EVENTS(X) \
  X(OVERCURRENT, 0x00, SYS_EVENT_NOTIFY)  /* @alias Over-current @description The channel drew more than its current limit */ \
  X(THERMAL_OR_UVLO, 0x01, SYS_EVENT_NOTIFY)  /* @alias Overheat or low supply @description The chip overheated or its supply dropped: every channel stopped */ \
  X(EXTERNAL, 0x02, SYS_EVENT_NOTIFY)  /* @alias External fault @description An external fault input fired */
// clang-format on

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
