#pragma once

#include "sys_power_monitor_id.h"
#include "sys_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The power monitor contract, PMON: 0x04xx, where 0x04 is the contract and xx its op. Readings of a measured channel
 * and its hardware current alerts. A device implements the ops it can (OPS lists them); a channel it doesn't have
 * answers ERR_POWER_BAD_CHANNEL. Readings are signed: a reversed shunt reports a negative current, not a wrapped one.
 * Readings are allowed while suspended.
 * VOLTAGE - the channel's bus voltage in mV
 * CURRENT - the channel's current in mA
 * ALERT   - arms a hardware current alert of the channel at a threshold in mA (sys_power_alert_e)
 * POWER   - the channel's power in mW
 * Events  - OCP_WARNING, OCP_CRITICAL (PMON_EVENTS below)
 * @usage int32_t mA; ERR_TRY(SYS_DEVICE_GET(ina, CONTRACT_PMON, OP_CURRENT, &mA, .channel = 1));
 *        ERR_TRY(SYS_DEVICE_SET(ina, CONTRACT_PMON, OP_ALERT, .channel = 1, .alert = SYS_POWER_ALERT_OCP_WARNING,
 *                               .threshold_mA = 1500));
 */

/* Available current alerts of a monitor channel */
//#ref-enum @alias Current Alert
typedef enum sys_power_alert_e {
  SYS_POWER_ALERT_OCP_WARNING = 0,   //@alias Warning @description OCP warning threshold
  SYS_POWER_ALERT_OCP_CRITICAL = 1,  //@alias Critical @description OCP critical threshold
} sys_power_alert_e;

/** @brief VOLTAGE / CURRENT / POWER key: a measured channel */
typedef struct __attribute__((packed)) {
  uint8_t channel;  /**< Measured channel index */
} pmon_channel_args_t;

/** @brief ALERT args */
typedef struct __attribute__((packed)) {
  uint8_t channel;       /**< Measured channel index */
  uint8_t alert;         /**< sys_power_alert_e */
  int32_t threshold_mA;  /**< Current threshold in mA */
} pmon_alert_args_t;

/**
 * @brief The contract, once: X(P, OP_NAME, op number, key_t, value_t, flags). Sides: VOLTAGE R, CURRENT R, ALERT W,
 * POWER R.
 */
#define CONTRACT_PMON_OPERATIONS(X, P)                                       \
  X(P, OP_VOLTAGE, 0x00, pmon_channel_args_t, int32_t,           0) \
  X(P, OP_CURRENT, 0x01, pmon_channel_args_t, int32_t,           0) \
  X(P, OP_ALERT,   0x02, cmd_none_t,          pmon_alert_args_t, 0) \
  X(P, OP_POWER,   0x03, pmon_channel_args_t, int32_t,           0)

SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_PMON, MODULE_ID_PMON)

/* Events of the contract, published by a device for an armed alert: source = the device, channel = the measured
 * channel, the payload the current that crossed it. Both matter to the user: NOTIFY. */
#define PMON_EVENTS(X, P)                                     \
  X(P, OCP_WARNING,  0x01, SYS_EVENT_NOTIFY, { int32_t mA; }) \
  X(P, OCP_CRITICAL, 0x02, SYS_EVENT_NOTIFY, { int32_t mA; })

SYS_EVENTS_DECLARE(PMON, MODULE_ID_PMON, DEVICE)

#ifdef __cplusplus
}
#endif
