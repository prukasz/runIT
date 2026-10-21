#pragma once

#include "sys_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The POWER_MONITOR contract (SYS_DEVICE_CONTRACT_POWER_MONITOR: 0x04xx):
 * readings of a measured channel and its hardware current alerts. A device
 * implements the ops it can (OPS lists them); a channel it doesn't have answers
 * ERR_POWER_BAD_CHANNEL. Readings are signed: a reversed shunt reports a
 * negative current, not a wrapped one. Readings are allowed while suspended.
 */

/* Available current alerts of a monitor channel */
//#ref-enum @alias Current Alert
typedef enum sys_power_alert_e {
  SYS_POWER_ALERT_OCP_WARNING = 0,   //@alias Warning @description OCP warning threshold
  SYS_POWER_ALERT_OCP_CRITICAL = 1,  //@alias Critical @description OCP critical threshold
} sys_power_alert_e;

/* Events of the monitor contract (sys_event.h "Events tables"), published by a
 * device for an armed alert; channel = the measured channel, value = mA. The n of
 * each event is the sys_power_alert_e that armed it. Both matter to the user: NOTIFY. */
#define SYS_POWER_MONITOR_EVENTS(X)       \
  X(OCP_WARNING,  0x00, SYS_EVENT_NOTIFY) \
  X(OCP_CRITICAL, 0x01, SYS_EVENT_NOTIFY)

#define SYS_POWER_MONITOR__EV(name, n, flags) \
  PMON_EV_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_POWER_MONITOR, n), PMON_EV_##name##_FLAGS = (flags),

enum { SYS_POWER_MONITOR_EVENTS(SYS_POWER_MONITOR__EV) };

typedef struct __attribute__((packed)) {
  uint8_t channel;  /**< Measured channel index */
} pmon_channel_args_t;

typedef struct __attribute__((packed)) {
  uint8_t channel;       /**< Measured channel index */
  uint8_t alert;         /**< sys_power_alert_e */
  int32_t threshold_mA;  /**< Current threshold in mA */
} pmon_alert_args_t;

/**
 * @brief The contract, once: X(P, NAME, op number, key_t, value_t). The read takes key_t and answers value_t, the write
 * takes value_t (the mirror). SYS_DEVICE_CONTRACT_DECLARE gives PMON_OP_<NAME>, PMON_key_<NAME>_t and
 * PMON_value_<NAME>_t; a driver writes OP_READ(PMON, NAME) / OP_WRITE(PMON, NAME) and lists OP_ENTRY(PMON, NAME),
 * OP_ENTRY_R, OP_ENTRY_W (SYS_DEVICE.MD "Contract tables"). Sides: VOLTAGE R, CURRENT R, ALERT W, POWER R. Numbers are
 * never reused.
 */
#define SYS_POWER_MONITOR_CONTRACT(X, P)                      \
  X(P, VOLTAGE, 0x00, pmon_channel_args_t, int32_t)           \
  X(P, CURRENT, 0x01, pmon_channel_args_t, int32_t)           \
  X(P, ALERT,   0x02, sys_device_none_t,   pmon_alert_args_t) \
  X(P, POWER,   0x03, pmon_channel_args_t, int32_t)

#define PMON_CONTRACT SYS_DEVICE_CONTRACT_POWER_MONITOR
SYS_DEVICE_CONTRACT_DECLARE(SYS_POWER_MONITOR_CONTRACT, PMON)

/**
 * @brief Wrappers: the sys_device_call of the op, nothing else.
 * @usage int32_t mW; ERR_TRY(sys_power_monitor_get_power(INA_ID, 0, &mW));
 */
MUST_USE err_h sys_power_monitor_get_voltage(uint16_t device, uint8_t channel, int32_t* out_mV);
MUST_USE err_h sys_power_monitor_get_current(uint16_t device, uint8_t channel, int32_t* out_mA);
MUST_USE err_h sys_power_monitor_get_power(uint16_t device, uint8_t channel, int32_t* out_mW);
MUST_USE err_h sys_power_monitor_set_alert(uint16_t device, uint8_t channel, sys_power_alert_e alert,
                                           int32_t threshold_mA);

#ifdef __cplusplus
}
#endif
