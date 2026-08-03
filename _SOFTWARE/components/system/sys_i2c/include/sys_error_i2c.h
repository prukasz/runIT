#pragma once
#include <stdint.h>
#include <stdio.h>

#define SYS_I2C_OWNER_MAP(X)                                            \
  X(OWNER_SYS_I2C_BASE, 0xA200, "OWNER_SYS_I2C_BASE")                   \
  X(OWNER_SYS_I2C_INIT, 0xA201, "OWNER_SYS_I2C_INIT")                   \
  X(OWNER_SYS_I2C_ADD_DRIVER, 0xA202, "OWNER_SYS_I2C_ADD_DRIVER")       \
  X(OWNER_SYS_I2C_REMOVE_DRIVER, 0xA203, "OWNER_SYS_I2C_REMOVE_DRIVER") \
  X(OWNER_SYS_I2C_DEVICE_PRESENT, 0xA204, "OWNER_SYS_I2C_DEVICE_PRESENT")

#define SYS_ERROR_I2C_MAP(X) X(ERR_I2C_DEV_NOT_FOUND, struct { uint8_t device_address; })

/** @brief Human-readable descriptions for the sys_i2c tags - see SE_describe_payload() in sys_error.h. */
#define SYS_ERROR_I2C_LOGGER_MAP(X) X(ERR_I2C_DEV_NOT_FOUND)

#define LOG_BODY_ERR_I2C_DEV_NOT_FOUND(p, out, out_size) snprintf((out), (out_size), "no I2C device responded at address 0x%02X", (p)->device_address)
