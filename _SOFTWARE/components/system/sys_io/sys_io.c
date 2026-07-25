#include "sys_io.h"
#include <stdint.h>
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"

static const char* TAG = __FILE_NAME__;

// Diagnostic macro
#define RP_IF_FEATURE_UNAVAILABLE(dev_id, pin_num)                   \
  do {                                                               \
    SE_RET_ERR(ERR_IO_PIN_FEATURE_UNSUPPORTED, (dev_id), (pin_num)); \
  } while (0)

const char* const sys_io_mode_e_to_string[] = {"INPUT", "INPUT_PULLUP", "INPUT_PULLDOWN", "OUTPUT_PUSH_PULL", "OUTPUT_OPEN_DRAIN", "OUTPUT_OPEN_DRAIN_PULLUP", "PWM", "ADC", "DAC"};

const char* const sys_io_intr_mode_e_to_string[] = {"DISABLE", "RISING_EDGE", "FALLING_EDGE", "BOTH_EDGES", "ADC_WINDOW_OUTSIDE", "ADC_WINDOW_INSIDE"};

// Custom dispatch macro that enforces the protected_pins check
#define SYS_IO_DISPATCH(dev_id, func_name, pin_num, ...)                                                      \
  do {                                                                                                        \
    sys_device_t* __disp_dev = sys_device_get_by_id((dev_id));                                                \
    if (__disp_dev == NULL) {                                                                                 \
      SE_RET_ERR(ERR_DEV_NOT_FOUND, dev_id);                                                                  \
    }                                                                                                         \
    if (!SYS_DEV_IS_INSTALLED(__disp_dev)) {                                                                  \
      SE_RET_ERR(ERR_DEV_NOT_INSTALLED, dev_id);                                                              \
    }                                                                                                         \
    if (SYS_DEV_IS_SUSPENDED(__disp_dev)) {                                                                   \
      SE_RET_ERR(ERR_DEV_SUSPENDED, dev_id);                                                                  \
    }                                                                                                         \
    IF_SYS_DEV_AND_FEATURE(dev_id, SYS_DEVICE_CONTRACT_IO, sys_io_vtable_t, func_name, dev_ptr, vtable_ptr) { \
      if (vtable_ptr->protected_pins & (1ULL << (pin_num))) {                                                 \
        SE_RET_ERR(ERR_IO_PIN_LOCKED, dev_id, pin_num);                                                       \
      }                                                                                                       \
      SE_RET_IF_ERR(vtable_ptr->func_name(dev_ptr->device_handle, pin_num, ##__VA_ARGS__));                   \
      return NULL;                                                                                            \
    }                                                                                                         \
    SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);                                                                    \
  } while (0)

#undef OWNER
#define OWNER OWNER_SYS_IO_REGISTER_DRIVER
err_h sys_io_register_driver(uint8_t device_id, void* handle, sys_io_vtable_t* dispatch_table) {
  SE_CHECK_NOT_NULL(dispatch_table);
  SE_CHECK_HANDLE(handle);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);

  dev->device_handle = handle;
  dev->contracts[SYS_DEVICE_CONTRACT_IO] = (void*)dispatch_table;
  ESP_LOGI(TAG, "Driver registered for IO device_id: %u", device_id);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_MODE
err_h sys_io_set_mode(uint8_t device_id, sys_io_pin_num_t pin_num, sys_io_mode_e mode) {
  SYS_IO_DISPATCH(device_id, io_set_mode, pin_num, mode);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_MODE
err_h sys_io_reset(uint8_t device_id, sys_io_pin_num_t pin_num) {
  SYS_IO_DISPATCH(device_id, io_reset, pin_num);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_CONFIGURE_INTR
err_h sys_io_configure_intr(uint8_t device_id, sys_io_pin_num_t pin_num, const sys_io_intr_config_t* config) {
  SYS_IO_DISPATCH(device_id, io_configure_intr, pin_num, config);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_LEVEL
err_h sys_io_set_level(uint8_t device_id, sys_io_pin_num_t pin_num, bool level) {
  SYS_IO_DISPATCH(device_id, io_set_level, pin_num, level);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_GET_LEVEL
err_h sys_io_get_level(uint8_t device_id, sys_io_pin_num_t pin_num, bool* level) {
  SE_CHECK_NOT_NULL(level);
  SYS_IO_DISPATCH(device_id, io_get_level, pin_num, level);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_TOGGLE
err_h sys_io_toggle(uint8_t device_id, sys_io_pin_num_t pin_num) {
  SYS_IO_DISPATCH(device_id, io_toggle, pin_num);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_GET_VOLTAGE
err_h sys_io_get_voltage(uint8_t device_id, sys_io_pin_num_t pin_num, uint32_t* out_mV) {
  SE_CHECK_NOT_NULL(out_mV);
  SYS_IO_DISPATCH(device_id, io_get_voltage, pin_num, out_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_VOLTAGE
err_h sys_io_set_voltage(uint8_t device_id, sys_io_pin_num_t pin_num, uint32_t voltage_mV) {
  SYS_IO_DISPATCH(device_id, io_set_voltage, pin_num, voltage_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_PWM_FREQUENCY
err_h sys_io_set_pwm_frequency(uint8_t device_id, sys_io_pin_num_t pin_num, uint32_t frequency_HZ) {
  SYS_IO_DISPATCH(device_id, io_set_pwm_frequency, pin_num, frequency_HZ);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_PWM_DUTY
err_h sys_io_set_pwm_duty(uint8_t device_id, sys_io_pin_num_t pin_num, uint32_t duty) {
  SYS_IO_DISPATCH(device_id, io_set_pwm_duty, pin_num, duty);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_UNREGISTER_DRIVER
err_h sys_io_unregister_driver(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (dev) {
    dev->contracts[SYS_DEVICE_CONTRACT_IO] = NULL;
  }
  return NULL;
}
