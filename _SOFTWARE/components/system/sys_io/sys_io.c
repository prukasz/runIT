#include "sys_io.h"
#include "sys_device.h"
#include <sdkconfig.h>


static const char* TAG = "SYS_IO";

const char* const sys_io_mode_e_to_string[] = {"INPUT", "INPUT_PULLUP", "INPUT_PULLDOWN", "OUTPUT_PUSH_PULL", "OUTPUT_OPEN_DRAIN", "OUTPUT_OPEN_DRAIN_PULLUP", "PWM", "ADC", "DAC"};

const char* const sys_io_intr_mode_e_to_string[] = {"DISABLE", "RISING_EDGE", "FALLING_EDGE", "BOTH_EDGES", "ADC_WINDOW_OUTSIDE", "ADC_WINDOW_INSIDE"};

const char* const sys_io_feature_e_to_string[] = {"RESET", "SET_MODE", "CONFIGURE_INTR", "SET_LEVEL", "GET_LEVEL", "TOGGLE", "GET_VOLTAGE", "SET_VOLTAGE", "SET_PWM_FREQUENCY", "SET_PWM_DUTY"};

// Dummy callback-event handler for CONFIG_SYS_CB_ROUTE_IO - logs and nothing else,
// a placeholder until sys_io has something real to route IO events to.
static void sys_io_cb_dummy_log(const cb_event_t* event) {
  if (event->head.callback_type != CALLBACK_IO) return;
  ESP_LOGI(TAG, "IO event: device %u, pin %u, event %u, val %ld", event->event.io.device_id, event->event.io.pin_id, event->event.io.trigger_event, (long)event->event.io.trigger_value);
}

__attribute__((constructor)) static void sys_io_cb_route_register(void) {
  SE_release(sys_cb_register_route(CONFIG_SYS_CB_ROUTE_IO, sys_io_cb_dummy_log));
}

// Custom dispatch macro that enforces the protected_pins check
#define SYS_IO_DISPATCH(dev_id, func_name, feature_id, pin_num, ...)                                          \
  do {                                                                                                        \
    if ((pin_num) >= 64u) {                                                                                   \
      SE_RET_ERR(ERR_IO_PIN_UNAVAILABLE, dev_id, pin_num);                                                    \
    }                                                                                                         \
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
      SE_PASS_ON_ERR(vtable_ptr->func_name(dev_ptr->device_handle, pin_num, ##__VA_ARGS__), ERR_DEV_DEP_FAILED, (dev_id));                   \
      return NULL;                                                                                            \
    }                                                                                                         \
    SE_RET_ERR(ERR_DEV_FEATURE_UNAVAILABLE, dev_id, SYS_DEVICE_CONTRACT_IO, (uint8_t)(feature_id));           \
  } while (0)

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_MODE
err_h sys_io_set_mode(sys_io_pin_ref_t ref) {
  SYS_IO_DISPATCH(ref.device_id, io_set_mode, SYS_IO_FEATURE_SET_MODE, ref.pin, ref.mode);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_MODE
err_h sys_io_reset(sys_io_pin_ref_t ref) {
  SYS_IO_DISPATCH(ref.device_id, io_reset, SYS_IO_FEATURE_RESET, ref.pin);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_CONFIGURE_INTR
err_h sys_io_configure_intr(sys_io_pin_ref_t ref, const sys_io_intr_config_t* config) {
  SE_CHECK_NOT_NULL(config);
  SYS_IO_DISPATCH(ref.device_id, io_configure_intr, SYS_IO_FEATURE_CONFIGURE_INTR, ref.pin, config);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_LEVEL
err_h sys_io_set_level(sys_io_pin_ref_t ref, bool level) {
  SYS_IO_DISPATCH(ref.device_id, io_set_level, SYS_IO_FEATURE_SET_LEVEL, ref.pin, level);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_GET_LEVEL
err_h sys_io_get_level(sys_io_pin_ref_t ref, bool* level) {
  SE_CHECK_NOT_NULL(level);
  SYS_IO_DISPATCH(ref.device_id, io_get_level, SYS_IO_FEATURE_GET_LEVEL, ref.pin, level);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_TOGGLE
err_h sys_io_toggle(sys_io_pin_ref_t ref) {
  SYS_IO_DISPATCH(ref.device_id, io_toggle, SYS_IO_FEATURE_TOGGLE, ref.pin);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_GET_VOLTAGE
err_h sys_io_get_voltage(sys_io_pin_ref_t ref, uint32_t* out_mV) {
  SE_CHECK_NOT_NULL(out_mV);
  SYS_IO_DISPATCH(ref.device_id, io_get_voltage, SYS_IO_FEATURE_GET_VOLTAGE, ref.pin, out_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_VOLTAGE
err_h sys_io_set_voltage(sys_io_pin_ref_t ref, uint32_t voltage_mV) {
  SYS_IO_DISPATCH(ref.device_id, io_set_voltage, SYS_IO_FEATURE_SET_VOLTAGE, ref.pin, voltage_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_PWM_FREQUENCY
err_h sys_io_set_pwm_frequency(sys_io_pin_ref_t ref, uint32_t frequency_HZ) {
  SYS_IO_DISPATCH(ref.device_id, io_set_pwm_frequency, SYS_IO_FEATURE_SET_PWM_FREQUENCY, ref.pin, frequency_HZ);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_PWM_DUTY
err_h sys_io_set_pwm_duty(sys_io_pin_ref_t ref, uint32_t duty) {
  SYS_IO_DISPATCH(ref.device_id, io_set_pwm_duty, SYS_IO_FEATURE_SET_PWM_DUTY, ref.pin, duty);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_BASE
err_h sys_io_lock_pin(sys_io_pin_ref_t ref) {
  if (ref.pin >= 64u) {
    SE_RET_ERR(ERR_IO_PIN_UNAVAILABLE, ref.device_id, ref.pin);
  }
  sys_device_t* dev = sys_device_get_by_id(ref.device_id);
  if (dev == NULL) {
    SE_RET_ERR(ERR_DEV_NOT_FOUND, ref.device_id);
  }
  sys_io_vtable_t* vtable = (sys_io_vtable_t*)SYS_DEV_GET_CONTRACT(dev, SYS_DEVICE_CONTRACT_IO);
  if (vtable == NULL) {
    SE_RET_ERR(ERR_DEV_FEATURE_UNAVAILABLE, ref.device_id, SYS_DEVICE_CONTRACT_IO, 0);
  }
  vtable->protected_pins |= (1ULL << ref.pin);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_IO_BASE
err_h sys_io_unlock_pin(sys_io_pin_ref_t ref) {
  if (ref.pin >= 64u) {
    SE_RET_ERR(ERR_IO_PIN_UNAVAILABLE, ref.device_id, ref.pin);
  }
  sys_device_t* dev = sys_device_get_by_id(ref.device_id);
  if (dev == NULL) {
    SE_RET_ERR(ERR_DEV_NOT_FOUND, ref.device_id);
  }
  sys_io_vtable_t* vtable = (sys_io_vtable_t*)SYS_DEV_GET_CONTRACT(dev, SYS_DEVICE_CONTRACT_IO);
  if (vtable == NULL) {
    SE_RET_ERR(ERR_DEV_FEATURE_UNAVAILABLE, ref.device_id, SYS_DEVICE_CONTRACT_IO, 0);
  }
  vtable->protected_pins &= ~(1ULL << ref.pin);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_IO_BASE
bool sys_io_temp_unlock(sys_io_pin_ref_t ref) {
  if (ref.pin >= 64u) return false;
  sys_device_t* dev = sys_device_get_by_id(ref.device_id);
  if (dev == NULL) return false;
  sys_io_vtable_t* vtable = (sys_io_vtable_t*)SYS_DEV_GET_CONTRACT(dev, SYS_DEVICE_CONTRACT_IO);
  if (vtable == NULL) return false;

  uint64_t mask = (1ULL << ref.pin);
  if (vtable->protected_pins & mask) {
    vtable->protected_pins &= ~mask;
    return true;
  }
  return false;
}

#undef OWNER
#define OWNER OWNER_SYS_IO_BASE
void sys_io_restore_lock(sys_io_pin_ref_t ref, bool was_locked) {
  if (!was_locked) return;
  if (ref.pin >= 64u) return;
  sys_device_t* dev = sys_device_get_by_id(ref.device_id);
  if (dev == NULL) return;
  sys_io_vtable_t* vtable = (sys_io_vtable_t*)SYS_DEV_GET_CONTRACT(dev, SYS_DEVICE_CONTRACT_IO);
  if (vtable == NULL) return;

  vtable->protected_pins |= (1ULL << ref.pin);
}

__attribute__((weak)) err_h sys_io_handle_fault(err_h node, err_h chain) {
  (void)chain;
  if (!node || SE_get_tag_level(node->tag) != SYS_DEV_ERR_CRITICAL) {
    return NULL;
  }
  // Severe IO fault containment
  return NULL;
}

