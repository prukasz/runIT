#include "sys_io.h"
#include "sys_device.h"
#include <sdkconfig.h>


const char* const sys_io_mode_e_to_string[] = {"INPUT", "INPUT_PULLUP", "INPUT_PULLDOWN", "OUTPUT_PUSH_PULL", "OUTPUT_OPEN_DRAIN", "OUTPUT_OPEN_DRAIN_PULLUP", "PWM", "ADC", "DAC"};

const char* const sys_io_intr_mode_e_to_string[] = {"DISABLE", "RISING_EDGE", "FALLING_EDGE", "BOTH_EDGES", "ADC_WINDOW_OUTSIDE", "ADC_WINDOW_INSIDE"};

const char* const sys_io_feature_names[] = {"reset", "set_mode", "configure_intr", "set_level", "get_level", "toggle",
                                            "get_voltage", "set_voltage", "set_pwm_frequency", "set_pwm_duty", NULL};
_Static_assert(sizeof(sys_io_feature_names) / sizeof(sys_io_feature_names[0]) - 1 == sizeof(sys_io_contract_t) / sizeof(void (*)(void)),
               "sys_io_feature_names must list every sys_io_contract_t member in order");

/* SYS_DEV_DISPATCH plus the pin range check and the per-instance pin lock
   (dev->io_locked_pins). The pin is the contract function's first argument. */
#define SYS_IO_DISPATCH(ref, func_name, ...)                                                                        \
  do {                                                                                                              \
    if ((ref).pin >= 64u) SE_FAIL(ERR_IO_PIN_UNAVAILABLE, (ref).device_id, (ref).pin);                           \
    SYS_DEV_RESOLVE((ref).device_id, SYS_DEVICE_CONTRACT_IO, sys_io_contract_t, func_name, __disp_dev, __disp_contract); \
    if (__disp_dev->io_locked_pins & (1ULL << (ref).pin)) SE_FAIL(ERR_IO_PIN_LOCKED, (ref).device_id, (ref).pin); \
    SE_TRY_WRAP(__disp_contract->func_name(__disp_dev->device_handle, (ref).pin, ##__VA_ARGS__), ERR_DEV_DEP_FAILED, \
                   .dev_id = (ref).device_id);                                                                      \
    return NULL;                                                                                                    \
  } while (0)

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_MODE
err_h sys_io_set_mode(sys_io_pin_ref_t ref) {
  SYS_IO_DISPATCH(ref, set_mode, ref.mode);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_RESET
err_h sys_io_reset(sys_io_pin_ref_t ref) {
  SYS_IO_DISPATCH(ref, reset);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_CONFIGURE_INTR
err_h sys_io_configure_intr(sys_io_pin_ref_t ref, const sys_io_intr_config_t* config) {
  SE_CHECK_NOT_NULL(config);
  SYS_IO_DISPATCH(ref, configure_intr, config);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_LEVEL
err_h sys_io_set_level(sys_io_pin_ref_t ref, bool level) {
  SYS_IO_DISPATCH(ref, set_level, level);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_GET_LEVEL
err_h sys_io_get_level(sys_io_pin_ref_t ref, bool* level) {
  SE_CHECK_NOT_NULL(level);
  SYS_IO_DISPATCH(ref, get_level, level);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_TOGGLE
err_h sys_io_toggle(sys_io_pin_ref_t ref) {
  SYS_IO_DISPATCH(ref, toggle);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_GET_VOLTAGE
err_h sys_io_get_voltage(sys_io_pin_ref_t ref, int32_t* out_mV) {
  SE_CHECK_NOT_NULL(out_mV);
  SYS_IO_DISPATCH(ref, get_voltage, out_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_VOLTAGE
err_h sys_io_set_voltage(sys_io_pin_ref_t ref, uint32_t voltage_mV) {
  SYS_IO_DISPATCH(ref, set_voltage, voltage_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_PWM_FREQUENCY
err_h sys_io_set_pwm_frequency(sys_io_pin_ref_t ref, uint32_t frequency_Hz) {
  SYS_IO_DISPATCH(ref, set_pwm_frequency, frequency_Hz);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_SET_PWM_DUTY
err_h sys_io_set_pwm_duty(sys_io_pin_ref_t ref, uint32_t duty) {
  SYS_IO_DISPATCH(ref, set_pwm_duty, duty);
}

#undef OWNER
#define OWNER OWNER_SYS_IO_BASE
/* Resolves the device owning ref for a lock operation: the pin must be in
 * range and the device must provide the IO contract. Locks live on the
 * instance (sys_device_t.io_locked_pins), so two devices of the same type
 * never share lock state. */
static SE_MUST_USE err_h io_lock_resolve(sys_io_pin_ref_t ref, sys_device_t** out_dev) {
  if (ref.pin >= 64u) SE_FAIL(ERR_IO_PIN_UNAVAILABLE, ref.device_id, ref.pin);
  sys_device_t* dev = sys_device_get_by_id(ref.device_id);
  if (dev == NULL) SE_FAIL(ERR_DEV_NOT_FOUND, ref.device_id);
  if (SYS_DEV_GET_CONTRACT(dev, SYS_DEVICE_CONTRACT_IO) == NULL) {
    SE_FAIL(ERR_DEV_FEATURE_UNAVAILABLE, ref.device_id, SYS_DEVICE_CONTRACT_IO, 0);
  }
  *out_dev = dev;
  return NULL;
}

err_h sys_io_lock_pin(sys_io_pin_ref_t ref) {
  sys_device_t* dev;
  SE_TRY(io_lock_resolve(ref, &dev));
  dev->io_locked_pins |= (1ULL << ref.pin);
  return NULL;
}

err_h sys_io_unlock_pin(sys_io_pin_ref_t ref) {
  sys_device_t* dev;
  SE_TRY(io_lock_resolve(ref, &dev));
  dev->io_locked_pins &= ~(1ULL << ref.pin);
  return NULL;
}

static bool io_temp_unlock(sys_io_pin_ref_t ref) {
  sys_device_t* dev;
  err_h err = io_lock_resolve(ref, &dev);
  if (err) {
    SE_release(err);
    return false;
  }
  uint64_t mask = (1ULL << ref.pin);
  if (dev->io_locked_pins & mask) {
    dev->io_locked_pins &= ~mask;
    return true;
  }
  return false;
}

static void io_restore_lock(sys_io_pin_ref_t ref, bool was_locked) {
  if (!was_locked) return;
  sys_device_t* dev;
  err_h err = io_lock_resolve(ref, &dev);
  if (err) {
    SE_release(err);
    return;
  }
  dev->io_locked_pins |= (1ULL << ref.pin);
}

err_h sys_io_set_locked_level(sys_io_pin_ref_t ref, bool level) {
  bool was_locked = io_temp_unlock(ref);
  err_h err = sys_io_set_level(ref, level);
  io_restore_lock(ref, was_locked);
  return err;
}

#undef OWNER
#define OWNER OWNER_SYS_IO_CONFIGURE_INTR
err_h sys_io_subscribe_pin(sys_io_pin_ref_t ref, sys_event_handler_f handler, void* ctx, uint8_t* out_id) {
  SE_CHECK_NOT_NULL(handler);
  sys_event_subscription_t sub = {
      .domain = SYS_EVENT_DOMAIN_IO,
      .device_id = ref.device_id,
      .channel = ref.pin,
      .event = SYS_EVENT_ANY,
      .inline_call = true,
      .handler = handler,
      .ctx = ctx,
  };
  return sys_event_subscribe(&sub, out_id);
}
