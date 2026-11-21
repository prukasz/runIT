#pragma once

#include "sys_power_usb_pd_id.h"
#include "sys_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The USB-PD sink contract, PD: 0x07xx, where 0x07 is the contract and xx its op. A USB Power Delivery sink negotiating
 * a voltage / current with its charger.
 * SETTINGS - the profile asked of the charger, set and read (the charger may not grant exactly what is asked)
 * OPTIONS  - what the charger offers: an array of sys_power_usb_pd_option_t (sys_power_usb_pd_list_options)
 * LIMITS   - the highest the charger offers (the power manager's USB budget)
 * No events.
 * @usage ERR_TRY(SYS_DEVICE_SET(pd, CONTRACT_PD, OP_SETTINGS, .voltage_mV = 12000, .current_mA = 2000));
 *        sys_power_usb_pd_limits_t lim; ERR_TRY(SYS_DEVICE_GET(pd, CONTRACT_PD, OP_LIMITS, &lim));
 */

/* A source offers at most 7 SPR + 6 EPR power data objects. */
#define SYS_POWER_USB_PD_OPTIONS_MAX 13

//#ref-enum @alias USB-PD Option Type
typedef enum {
  SYS_POWER_USB_PD_OPTION_FIXED = 0,  //@alias Fixed @description Fixed voltage supply PDO
  SYS_POWER_USB_PD_OPTION_PPS = 1,    //@alias PPS @description Programmable Power Supply PDO
  SYS_POWER_USB_PD_OPTION_AVS = 2,    //@alias AVS @description Adjustable Voltage Supply PDO
} sys_power_usb_pd_option_type_e;

/** @brief One power option the source offers: the OPTIONS answer is an array of these. */
typedef struct __attribute__((packed)) {
  uint8_t slot;     /**< 1-based offer index from source */
  uint8_t type;     /**< sys_power_usb_pd_option_type_e */
  uint16_t min_mV;  /**< Minimum output voltage in mV */
  uint16_t max_mV;  /**< Maximum output voltage in mV */
  uint16_t max_mA;  /**< Maximum current in mA */
} sys_power_usb_pd_option_t;

/** @brief SETTINGS args and answer: the profile asked of the charger */
typedef struct __attribute__((packed)) {
  uint32_t voltage_mV;  /**< Requested voltage in mV */
  uint32_t current_mA;  /**< Requested current in mA */
} pd_settings_args_t;

/** @brief What LIMITS answers */
typedef struct __attribute__((packed)) {
  int32_t voltage_mV;  /**< Highest-voltage offer in mV */
  int32_t current_mA;  /**< Offered current in mA */
} sys_power_usb_pd_limits_t;

/**
 * @brief The contract, once: X(P, OP_NAME, op number, key_t, value_t, flags). Sides: SETTINGS RW, OPTIONS R (an array of
 * its value_t, not one), LIMITS R.
 */
#define CONTRACT_PD_OPERATIONS(X, P)                                         \
  X(P, OP_SETTINGS, 0x00, cmd_none_t, pd_settings_args_t,        0) \
  X(P, OP_OPTIONS,  0x01, cmd_none_t, sys_power_usb_pd_option_t, 0) \
  X(P, OP_LIMITS,   0x02, cmd_none_t, sys_power_usb_pd_limits_t, 0)

SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_PD, MODULE_ID_PD)

/**
 * @brief Reads what the charger offers (op OPTIONS): an answer of any number of options, so not SYS_DEVICE_GET.
 * @param[in]  device      USB-PD device ID
 * @param[out] out         Array for the options
 * @param[in]  max_options Size of out, at most SYS_POWER_USB_PD_OPTIONS_MAX
 * @param[out] out_count   How many options were written
 * @return ERR_BAD_ARG for a NULL out or out_count, ERR_CMD_BAD_LEN for an answer that is not whole options, or the
 *         error of the device read
 */
MUST_USE err_h sys_power_usb_pd_list_options(uint8_t device, sys_power_usb_pd_option_t* out, uint8_t max_options,
                                             uint8_t* out_count);

#ifdef __cplusplus
}
#endif
