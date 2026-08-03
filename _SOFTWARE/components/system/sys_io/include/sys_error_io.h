#pragma once
#include <stdint.h>
#include <stdio.h>

#define SYS_IO_OWNER_MAP(X)                                                   \
  X(OWNER_SYS_IO_BASE, 0xA300, "OWNER_SYS_IO_BASE")                           \
  X(OWNER_SYS_IO_RESET, 0xA301, "OWNER_SYS_IO_RESET")                         \
  X(OWNER_SYS_IO_DRIVER_RESET, 0xA302, "OWNER_SYS_IO_DRIVER_RESET")           \
  X(OWNER_SYS_IO_SET_MODE, 0xA303, "OWNER_SYS_IO_SET_MODE")                   \
  X(OWNER_SYS_IO_CONFIGURE_INTR, 0xA304, "OWNER_SYS_IO_CONFIGURE_INTR")       \
  X(OWNER_SYS_IO_SET_LEVEL, 0xA305, "OWNER_SYS_IO_SET_LEVEL")                 \
  X(OWNER_SYS_IO_GET_LEVEL, 0xA306, "OWNER_SYS_IO_GET_LEVEL")                 \
  X(OWNER_SYS_IO_TOGGLE, 0xA307, "OWNER_SYS_IO_TOGGLE")                       \
  X(OWNER_SYS_IO_GET_VOLTAGE, 0xA308, "OWNER_SYS_IO_GET_VOLTAGE")             \
  X(OWNER_SYS_IO_SET_VOLTAGE, 0xA309, "OWNER_SYS_IO_SET_VOLTAGE")             \
  X(OWNER_SYS_IO_REGISTER_DRIVER, 0xA30A, "OWNER_SYS_IO_REGISTER_DRIVER")     \
  X(OWNER_SYS_IO_DESTROY, 0xA30B, "OWNER_SYS_IO_DESTROY")                     \
  X(OWNER_SYS_IO_SET_PWM_FREQUENCY, 0xA30C, "OWNER_SYS_IO_SET_PWM_FREQUENCY") \
  X(OWNER_SYS_IO_SET_PWM_DUTY, 0xA30D, "OWNER_SYS_IO_SET_PWM_DUTY")           \
  X(OWNER_SYS_IO_UNREGISTER_DRIVER, 0xA30E, "OWNER_SYS_IO_UNREGISTER_DRIVER")

#define SYS_ERROR_IO_MAP(X)                                                                \
  X(ERR_IO_PIN_UNCONFIGURED, struct { uint8_t dev_id; uint8_t pin_num; })                  \
  X(ERR_IO_PIN_UNAVAILABLE, struct { uint8_t dev_id; uint8_t pin_num; })                   \
  X(ERR_IO_PIN_ALREADY_IN_USE, struct { uint8_t dev_id; uint8_t pin_num; uint8_t mode; })   \
  X(ERR_IO_PIN_FEATURE_UNSUPPORTED, struct { uint8_t dev_id; uint8_t pin_num; })           \
  X(ERR_IO_PIN_LOCKED, struct { uint8_t dev_id; uint8_t pin_id; })                        \
  X(ERR_IO_PIN_MODE_UNSUPPORTED, struct { uint8_t dev_id; uint8_t pin_id; uint8_t mode; })

/**
 * @brief Human-readable descriptions for the sys_io tags - see
 * SE_describe_payload() in sys_error.h. `mode` fields are printed as plain
 * numbers rather than via sys_io_mode_e_to_string[] - this header can't
 * safely #include sys_io.h at this point in the include chain (see
 * sys_error_dev.h's LOGGER_MAP comment for the same constraint).
 */
#define SYS_ERROR_IO_LOGGER_MAP(X)     \
  X(ERR_IO_PIN_UNCONFIGURED)           \
  X(ERR_IO_PIN_UNAVAILABLE)            \
  X(ERR_IO_PIN_ALREADY_IN_USE)         \
  X(ERR_IO_PIN_FEATURE_UNSUPPORTED)    \
  X(ERR_IO_PIN_LOCKED)                 \
  X(ERR_IO_PIN_MODE_UNSUPPORTED)

#define LOG_BODY_ERR_IO_PIN_UNCONFIGURED(p, out, out_size) snprintf((out), (out_size), "pin %u on device %u is unconfigured", (p)->pin_num, (p)->dev_id)
#define LOG_BODY_ERR_IO_PIN_UNAVAILABLE(p, out, out_size) snprintf((out), (out_size), "pin %u is not available on device %u (out of range or unmapped)", (p)->pin_num, (p)->dev_id)
#define LOG_BODY_ERR_IO_PIN_ALREADY_IN_USE(p, out, out_size) \
  snprintf((out), (out_size), "pin %u on device %u is already in use (mode %u)", (p)->pin_num, (p)->dev_id, (p)->mode)
#define LOG_BODY_ERR_IO_PIN_FEATURE_UNSUPPORTED(p, out, out_size) snprintf((out), (out_size), "pin %u on device %u doesn't support this feature", (p)->pin_num, (p)->dev_id)
#define LOG_BODY_ERR_IO_PIN_LOCKED(p, out, out_size) snprintf((out), (out_size), "pin %u on device %u is locked (protected_pins)", (p)->pin_id, (p)->dev_id)
#define LOG_BODY_ERR_IO_PIN_MODE_UNSUPPORTED(p, out, out_size) \
  snprintf((out), (out_size), "pin %u on device %u doesn't support mode %u", (p)->pin_id, (p)->dev_id, (p)->mode)
