#include "sys_power.h"
#include "sys_device.h"
#include "sys_error.h"
#include <sdkconfig.h>

/* ========================================================================== *
 * WEWNĘTRZNE STRUKTURY STANU
 * ========================================================================== */
/* The power manager's state (source, budget, rails, battery) lives in
   sys_power_manager.c. This file dispatches the power contracts. */

#undef OWNER
#define OWNER OWNER_SYS_POWER_BASE

/* Member names of the power contracts in order, NULL-terminated (feature id = index). */
//@contract-features $SYS_DEVICE_CONTRACT_POWER_VREG
const char* const sys_power_vreg_feature_names[] = {"set_enable", "set_voltage", "set_current", NULL};
//@contract-features $SYS_DEVICE_CONTRACT_POWER_MONITOR
const char* const sys_power_monitor_feature_names[] = {"get_voltage", "get_current", "set_alert", NULL};
//@contract-features $SYS_DEVICE_CONTRACT_POWER_USB_PD
const char* const sys_power_usb_pd_feature_names[] = {"set_settings", "list_options", "get_limits", NULL};
_Static_assert(sizeof(sys_power_vreg_feature_names) / sizeof(sys_power_vreg_feature_names[0]) - 1 == sizeof(sys_power_vreg_contract_t) / sizeof(void (*)(void)),
               "sys_power_vreg_feature_names must list every sys_power_vreg_contract_t member in order");
_Static_assert(sizeof(sys_power_monitor_feature_names) / sizeof(sys_power_monitor_feature_names[0]) - 1 == sizeof(sys_power_monitor_contract_t) / sizeof(void (*)(void)),
               "sys_power_monitor_feature_names must list every sys_power_monitor_contract_t member in order");
_Static_assert(sizeof(sys_power_usb_pd_feature_names) / sizeof(sys_power_usb_pd_feature_names[0]) - 1 == sizeof(sys_power_usb_pd_contract_t) / sizeof(void (*)(void)),
               "sys_power_usb_pd_feature_names must list every sys_power_usb_pd_contract_t member in order");

/* ========================================================================== *
 * ZARZĄDZANIE BUDŻETEM (API SYSTEMOWE)
 * ========================================================================== */
/* Budget, source and battery: sys_power_manager.c (sys_power_init() and the
   budgeted sys_power_vreg_set_enable / set_voltage / set_current). */

/* ========================================================================== *
 * VREG API
 * ========================================================================== */

/* set_enable / set_voltage / set_current are budgeted: sys_power_manager.c. */

/* ========================================================================== *
 * MONITOR API
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_POWER_MONITOR_GET_VOLTAGE
err_h sys_power_monitor_get_voltage(uint8_t device_id, uint8_t channel, int32_t* out_mV) {
  SE_CHECK_NOT_NULL(out_mV);
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_MONITOR, sys_power_monitor_contract_t, get_voltage, channel, out_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_MONITOR_GET_CURRENT
err_h sys_power_monitor_get_current(uint8_t device_id, uint8_t channel, int32_t* out_mA) {
  SE_CHECK_NOT_NULL(out_mA);
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_MONITOR, sys_power_monitor_contract_t, get_current, channel, out_mA);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_MONITOR_SET_ALERT
err_h sys_power_monitor_set_alert(uint8_t device_id, uint8_t channel, sys_power_events_e alert, int32_t threshold_mA) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_MONITOR, sys_power_monitor_contract_t, set_alert, channel, alert, threshold_mA);
}

/* ========================================================================== *
 * USB PD API
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_POWER_USB_PD_SET
err_h sys_power_usb_pd_set(uint8_t device_id, uint32_t voltage_mV, uint32_t current_mA) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_USB_PD, sys_power_usb_pd_contract_t, set_settings, voltage_mV, current_mA);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_USB_PD_LIST
err_h sys_power_usb_pd_list(uint8_t device_id, sys_power_usb_pd_option_t* out_options, uint8_t max_options, uint8_t* out_count) {
  SE_CHECK_NOT_NULL(out_options);
  SE_CHECK_NOT_NULL(out_count);
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_USB_PD, sys_power_usb_pd_contract_t, list_options, out_options, max_options, out_count);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_USB_PD_GET_LIMITS
err_h sys_power_usb_pd_get_limits(uint8_t device_id, int32_t* out_mV, int32_t* out_mA) {
  SE_CHECK_NOT_NULL(out_mV);
  SE_CHECK_NOT_NULL(out_mA);

  SYS_DEV_RESOLVE(device_id, SYS_DEVICE_CONTRACT_POWER_USB_PD, sys_power_usb_pd_contract_t, get_limits, dev, usb_pd);

  SE_TRY_WRAP(usb_pd->get_limits(dev->device_handle, out_mV, out_mA), ERR_DEV_DEP_FAILED, .dev_id = device_id);
  // A negative limit can't size the budget.
  SE_CHECK_IN_RANGE_I32(*out_mV, 0, INT32_MAX);
  SE_CHECK_IN_RANGE_I32(*out_mA, 0, INT32_MAX);
  return NULL;
}
