#pragma once

#include <stdint.h>

#include "sys_device.h"
#include "sys_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The H-bridge contract, HBRIDGE: 0x09xx, where 0x09 is the contract and xx its op. A motor driver with channels. A
 * channel is a full bridge (a motor, both directions) or a half bridge (a solenoid, one direction), as the driver is
 * configured. Chip specifics (current limit, fault latch, ...) are the driver's own ops.
 * DRIVE   - drives a channel with a signed PWM value
 * BRAKE   - shorts the channel's outputs (the motor stops fast)
 * COAST   - lets the channel's outputs float (the motor runs free)
 * CURRENT - the channel's current in mA
 * Events  - OVERCURRENT, THERMAL_OR_UVLO, EXTERNAL (HBRIDGE_EVENTS below)
 * Drive is a signed number in PWM steps: -4095 (full reverse) .. +4095 (full forward) for a full bridge, 0 .. 4095 for
 * a half bridge: the 12 bits of IO_PWM_DUTY_MAX, so the driver loses no resolution between the contract and the PWM.
 * A full bridge brakes below 20 (0.5 %).
 * A contract only: the calls are SYS_DEVICE_SET / SYS_DEVICE_GET, the answers CMD_REPLY.
 * @usage ERR_TRY(SYS_DEVICE_SET(bridge, CONTRACT_HBRIDGE, OP_DRIVE, .channel = 0, .drive = -2048));
 *        ERR_TRY(SYS_DEVICE_SET(bridge, CONTRACT_HBRIDGE, OP_BRAKE, .channel = 0));
 *        int32_t mA; ERR_TRY(SYS_DEVICE_GET(bridge, CONTRACT_HBRIDGE, OP_CURRENT, &mA, .channel = 0));
 */

//#ref-enum @alias H-Bridge Fault
typedef enum sys_hbridge_fault_reason_e {
  SYS_HBRIDGE_FAULT_NONE = 0,             //@alias None @description No fault
  SYS_HBRIDGE_FAULT_OVERCURRENT = 1,      //@alias Overcurrent @description Overcurrent condition on channel
  SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO = 2,  //@alias Overheat or Low Supply @description Thermal shutdown or undervoltage lockout
  SYS_HBRIDGE_FAULT_EXTERNAL = 3,         //@alias External @description External fault input tripped
} sys_hbridge_fault_reason_e;

#define SYS_HBRIDGE_DRIVE_MAX 4095  // = IO_PWM_DUTY_MAX: 12 bits

/** @brief BRAKE / COAST args and the CURRENT key: a channel */
typedef struct __attribute__((packed)) {
  uint8_t channel;  /**< Motor channel index */
} hbridge_channel_args_t;

/** @brief DRIVE args: a channel and its drive */
typedef struct __attribute__((packed)) {
  uint8_t channel;  /**< Motor channel index */
  int16_t drive;    /**< PWM drive: -4095 (reverse) to +4095 (forward) */
} hbridge_drive_args_t;

/**
 * @brief The contract, once: X(P, OP_NAME, op number, key_t, value_t, flags). Sides: DRIVE W, BRAKE W, COAST W,
 * CURRENT R.
 */
#define CONTRACT_HBRIDGE_OPERATIONS(X, P)                                            \
  X(P, OP_DRIVE,   0x00, cmd_none_t,             hbridge_drive_args_t,   0) \
  X(P, OP_BRAKE,   0x01, cmd_none_t,             hbridge_channel_args_t, 0) \
  X(P, OP_COAST,   0x02, cmd_none_t,             hbridge_channel_args_t, 0) \
  X(P, OP_CURRENT, 0x03, hbridge_channel_args_t, int32_t,                0)

MODULE_ID_DECLARE(HBRIDGE, 0x09)
SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_HBRIDGE, HBRIDGE, 0x09)

/* Events of the contract: a channel fault, published by the driver; source = the device, channel = the channel, the
 * payload its current at the time. A fault is something the user must see: NOTIFY. */
#define HBRIDGE_EVENTS(X, P)                                     \
  X(P, OVERCURRENT,     0x01, SYS_EVENT_NOTIFY, { int32_t mA; }) \
  X(P, THERMAL_OR_UVLO, 0x02, SYS_EVENT_NOTIFY, { int32_t mA; }) \
  X(P, EXTERNAL,        0x03, SYS_EVENT_NOTIFY, { int32_t mA; })

SYS_EVENTS_DECLARE(HBRIDGE)

#ifdef __cplusplus
}
#endif
