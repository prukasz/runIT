#pragma once

#include "sys_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SYS_DEVICE_CONTRACT_POWER_MONITOR: 0x04xx, where 0x04 is the power monitor contract and xx its op. Readings of a
 * measured channel and its hardware current alerts. A device implements the ops it can (OPS lists them); a channel it
 * doesn't have answers ERR_POWER_BAD_CHANNEL. Readings are signed: a reversed shunt reports a negative current, not a
 * wrapped one. Readings are allowed while suspended.
 * VOLTAGE - the channel's bus voltage in mV
 * CURRENT - the channel's current in mA
 * ALERT   - arms a hardware current alert of the channel at a threshold in mA (sys_power_alert_e)
 * POWER   - the channel's power in mW
 * Events  - OCP_WARNING, OCP_CRITICAL (PMON_EVENTS below)
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
#define PMON_EVENTS(X, P)                    \
  X(P, OCP_WARNING,  0x00, SYS_EVENT_NOTIFY) \
  X(P, OCP_CRITICAL, 0x01, SYS_EVENT_NOTIFY)

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
 * @brief The contract, once: X(P, NAME, op number, key_t, value_t, flags). The read takes key_t and answers value_t,
 * the write takes value_t (the mirror). SYS_DEVICE_CONTRACT_DECLARE gives PMON_OP_<NAME>, PMON_key_<NAME>_t and
 * PMON_value_<NAME>_t; a driver writes OP_READ(PMON, NAME) / OP_WRITE(PMON, NAME) and lists OP_ENTRY(PMON, NAME),
 * OP_ENTRY_R, OP_ENTRY_W (SYS_DEVICE.MD "Contract tables"). Sides: VOLTAGE R, CURRENT R, ALERT W, POWER R. Numbers are
 * never reused.
 */
#define PMON_OPERATIONS(X, P)                                       \
  X(P, VOLTAGE, 0x00, pmon_channel_args_t,    int32_t,           0) \
  X(P, CURRENT, 0x01, pmon_channel_args_t,    int32_t,           0) \
  X(P, ALERT,   0x02, sys_device_args_none_t, pmon_alert_args_t, 0) \
  X(P, POWER,   0x03, pmon_channel_args_t,    int32_t,           0)

SYS_DEVICE_CONTRACT_DECLARE(PMON, SYS_DEVICE_CONTRACT_POWER_MONITOR)
SYS_EVENTS_DECLARE(PMON)

/***************************************************************
* C API: the sys_device_call of the op, nothing else
******************************************************************/

/**
 * @brief Reads the voltage of a channel (op VOLTAGE)
 * @param[in]  device  Monitor device id
 * @param[in]  channel Measured channel index
 * @param[out] out_mV  Voltage in mV
 * @return ERR_DEV_BAD_ARG for a NULL out_mV, ERR_POWER_BAD_CHANNEL for a channel it doesn't have, or the error of
 * the device read
 */
MUST_USE err_h sys_power_monitor_get_voltage(uint16_t device, uint8_t channel, int32_t* out_mV);

/**
 * @brief Reads the current of a channel, signed (op CURRENT)
 * @param[in]  device  Monitor device id
 * @param[in]  channel Measured channel index
 * @param[out] out_mA  Current in mA
 * @return ERR_DEV_BAD_ARG for a NULL out_mA, ERR_POWER_BAD_CHANNEL for a channel it doesn't have, or the error of
 * the device read
 */
MUST_USE err_h sys_power_monitor_get_current(uint16_t device, uint8_t channel, int32_t* out_mA);

/**
 * @brief Reads the power of a channel (op POWER)
 * @param[in]  device  Monitor device id
 * @param[in]  channel Measured channel index
 * @param[out] out_mW  Power in mW
 * @return ERR_DEV_BAD_ARG for a NULL out_mW, ERR_POWER_BAD_CHANNEL for a channel it doesn't have, or the error of
 * the device read
 * @usage int32_t mW; ERR_TRY(sys_power_monitor_get_power(INA_ID, 0, &mW));
 */
MUST_USE err_h sys_power_monitor_get_power(uint16_t device, uint8_t channel, int32_t* out_mW);

/**
 * @brief Arms a hardware current alert of a channel (op ALERT); the device publishes the matching event
 * @param[in] device       Monitor device id
 * @param[in] channel      Measured channel index
 * @param[in] alert        sys_power_alert_e
 * @param[in] threshold_mA Current threshold in mA
 * @return ERR_POWER_BAD_CHANNEL for a channel it doesn't have, or the error of the device write
 */
MUST_USE err_h sys_power_monitor_set_alert(uint16_t device, uint8_t channel, sys_power_alert_e alert,
                                           int32_t threshold_mA);

#ifdef __cplusplus
}
#endif
