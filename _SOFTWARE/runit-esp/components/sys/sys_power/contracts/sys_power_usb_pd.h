#pragma once

#include "sys_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The POWER_USB_PD contract (SYS_DEVICE_CONTRACT_POWER_USB_PD: 0x07xx): a USB Power Delivery sink negotiating
 * a voltage / current with its charger. What the charger offers is read with OPTIONS; a SETTINGS write asks for a
 * profile (the charger may not grant exactly what is asked); LIMITS gives the highest the charger offers (the
 * power manager's USB budget).
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
  uint8_t slot;       /**< 1-based offer index from source */
  uint8_t type;       /**< sys_power_usb_pd_option_type_e */
  uint16_t min_mV;    /**< Minimum output voltage in mV */
  uint16_t max_mV;    /**< Maximum output voltage in mV */
  uint16_t max_mA;    /**< Maximum current in mA */
} sys_power_usb_pd_option_t;

typedef struct __attribute__((packed)) {
  uint32_t voltage_mV;  /**< Requested voltage in mV */
  uint32_t current_mA;  /**< Requested current in mA */
} pd_settings_args_t;

typedef struct __attribute__((packed)) {
  int32_t voltage_mV;  /**< Highest-voltage offer in mV */
  int32_t current_mA;  /**< Offered current in mA */
} sys_power_usb_pd_limits_t;

/**
 * @brief The contract, once: X(P, NAME, op number, key_t, value_t). The read takes key_t and answers value_t, the write
 * takes value_t (the mirror). SYS_DEVICE_CONTRACT_DECLARE gives PD_OP_<NAME>, PD_key_<NAME>_t and PD_value_<NAME>_t; a
 * driver writes OP_READ(PD, NAME) / OP_WRITE(PD, NAME) and lists OP_ENTRY(PD, NAME), OP_ENTRY_R, OP_ENTRY_W
 * (SYS_DEVICE.MD "Contract tables"). Sides: SETTINGS RW, OPTIONS R, LIMITS R. Numbers are never reused.
 */
#define PD_OPERATIONS(X, P)                                          \
  X(P, SETTINGS, 0x00, sys_device_none_t, pd_settings_args_t)        \
  X(P, OPTIONS,  0x01, sys_device_none_t, sys_power_usb_pd_option_t) \
  X(P, LIMITS,   0x02, sys_device_none_t, sys_power_usb_pd_limits_t)

SYS_DEVICE_CONTRACT_DECLARE(PD, SYS_DEVICE_CONTRACT_POWER_USB_PD)

/**
 * @brief Wrappers: the sys_device_call of the op, nothing else.
 * @param max_options at most SYS_POWER_USB_PD_OPTIONS_MAX
 */
MUST_USE err_h sys_power_usb_pd_set_settings(uint16_t device, uint32_t voltage_mV, uint32_t current_mA);
MUST_USE err_h sys_power_usb_pd_list_options(uint16_t device, sys_power_usb_pd_option_t* out, uint8_t max_options,
                                             uint8_t* out_count);
MUST_USE err_h sys_power_usb_pd_get_limits(uint16_t device, int32_t* out_mV, int32_t* out_mA);

#ifdef __cplusplus
}
#endif
