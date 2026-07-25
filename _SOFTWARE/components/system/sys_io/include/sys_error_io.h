#pragma once
#include <stdint.h>

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
