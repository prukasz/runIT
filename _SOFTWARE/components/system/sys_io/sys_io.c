#include "sys_io.h"
#include "sys_device.h"
#include "esp_log.h"

static const char* TAG = __FILE_NAME__;

// Diagnostic macro
#define RP_IF_FEATURE_UNAVAILABLE(dev_id, pin_num)                                               \
  do {                                                                                           \
    ESP_LOGW(TAG, "%s is unavailable on device_id: %u, pin: %u", __func__, (dev_id), (pin_num)); \
    STA_RP(STA_W(ERR_SYS_IO_FEATURE_UNAVAILABLE, OWNER, (dev_id), STATUS_PAYLOAD_SYS_IO));                              \
  } while (0)

const char* const sys_io_mode_e_to_string[] = {"INPUT", "INPUT_PULLUP", "INPUT_PULLDOWN", "OUTPUT_PUSH_PULL", "OUTPUT_OPEN_DRAIN", "OUTPUT_OPEN_DRAIN_PULLUP", "PWM", "ADC", "DAC"};

const char* const sys_io_intr_mode_e_to_string[] = {"DISABLE", "RISING_EDGE", "FALLING_EDGE", "BOTH_EDGES", "ADC_WINDOW_OUTSIDE", "ADC_WINDOW_INSIDE"};

// Custom dispatch macro that enforces the protected_pins check
#define SYS_IO_DISPATCH(dev_id, func_name, pin_num, ...)                                                                                                        \
  do {                                                                                                                                                          \
    IF_SYS_DEV_AND_FEATURE(dev_id, SYS_DEVICE_CONTRACT_IO, sys_io_vtable_t, func_name, dev_ptr, vtable_ptr) {                                                       \
      if (vtable_ptr->protected_pins & (1ULL << (pin_num))) {                                                                                                   \
        return STA_C(ERR_SYS_IO_PIN_IN_OTHER_USE, OWNER, dev_id, STATUS_PAYLOAD_SYS_IO);                                                                                               \
      }                                                                                                                                                         \
      return vtable_ptr->func_name(dev_ptr->device_handle, pin_num, ##__VA_ARGS__);                                                                             \
    }                                                                                                                                                           \
    STA_RP(STA_C(ERR_NOT_SUPPORTED, OWNER, dev_id, STATUS_PAYLOAD_SYS_IO));                                                                                                          \
  } while (0)

#undef OWNER
#define OWNER OWNER_SYS_IO_REGISTER_DRIVER
status_rep_t sys_io_register_driver(uint8_t device_id, void* handle, sys_io_vtable_t* dispatch_table) {
  SYS_IO_CHECK_NOT_NULL_RP(dispatch_table);

  sys_device_t *dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEVICE_INSTALL_FAILED, OWNER, device_id, STATUS_PAYLOAD_SYS_IO);

  dev->device_handle = handle;
  dev->contracts[SYS_DEVICE_CONTRACT_IO] = (void*)dispatch_table;

  ESP_LOGI(TAG, "Driver registered for IO device_id: %u", device_id);
  return STA_OK;
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_MODE
status_rep_t sys_io_set_mode(uint8_t device_id, sys_io_pin_num_t pin_num, sys_io_mode_e mode) {
  SYS_IO_DISPATCH(device_id, io_set_mode, pin_num, mode);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_MODE
status_rep_t sys_io_reset(uint8_t device_id, sys_io_pin_num_t pin_num) {
  SYS_IO_DISPATCH(device_id, io_reset, pin_num);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_CONFIGURE_INTR
status_rep_t sys_io_configure_intr(uint8_t device_id, sys_io_pin_num_t pin_num, const sys_io_intr_config_t* config) {
  SYS_IO_CHECK_NOT_NULL_RP((void*)config);
  SYS_IO_DISPATCH(device_id, io_configure_intr, pin_num, config);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_LEVEL
status_rep_t sys_io_set_level(uint8_t device_id, sys_io_pin_num_t pin_num, bool level) {
  SYS_IO_DISPATCH(device_id, io_set_level, pin_num, level);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_GET_LEVEL
status_rep_t sys_io_get_level(uint8_t device_id, sys_io_pin_num_t pin_num, bool* level) {
  SYS_IO_CHECK_NOT_NULL_RP((void*)level);
  SYS_IO_DISPATCH(device_id, io_get_level, pin_num, level);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_TOGGLE
status_rep_t sys_io_toggle(uint8_t device_id, sys_io_pin_num_t pin_num) {
  SYS_IO_DISPATCH(device_id, io_toggle, pin_num);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_GET_VOLTAGE
status_rep_t sys_io_get_voltage(uint8_t device_id, sys_io_pin_num_t pin_num, uint32_t* out_mV) {
  SYS_IO_CHECK_NOT_NULL_RP((void*)out_mV);
  SYS_IO_DISPATCH(device_id, io_get_voltage, pin_num, out_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_VOLTAGE
status_rep_t sys_io_set_voltage(uint8_t device_id, sys_io_pin_num_t pin_num, uint32_t voltage_mV) {
  SYS_IO_DISPATCH(device_id, io_set_voltage, pin_num, voltage_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_PWM_FREQUENCY
status_rep_t sys_io_set_pwm_frequency(uint8_t device_id, sys_io_pin_num_t pin_num, uint32_t frequency_HZ) {
  SYS_IO_DISPATCH(device_id, io_set_pwm_frequency, pin_num, frequency_HZ);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_PWM_DUTY
status_rep_t sys_io_set_pwm_duty(uint8_t device_id, sys_io_pin_num_t pin_num, uint32_t duty) {
  SYS_IO_DISPATCH(device_id, io_set_pwm_duty, pin_num, duty);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_UNREGISTER_DRIVER
status_rep_t sys_io_unregister_driver(uint8_t device_id) {
  sys_device_t *dev = sys_device_get_by_id(device_id);
  if (dev) {
    dev->contracts[SYS_DEVICE_CONTRACT_IO] = NULL;
  }
  return STA_OK;
}