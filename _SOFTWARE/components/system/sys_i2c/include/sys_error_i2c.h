#pragma once
#include <stdint.h>

#define SYS_I2C_OWNER_MAP(X)                                            \
  X(OWNER_SYS_I2C_BASE, 0xA200, "OWNER_SYS_I2C_BASE")                   \
  X(OWNER_SYS_I2C_INIT, 0xA201, "OWNER_SYS_I2C_INIT")                   \
  X(OWNER_SYS_I2C_ADD_DRIVER, 0xA202, "OWNER_SYS_I2C_ADD_DRIVER")       \
  X(OWNER_SYS_I2C_REMOVE_DRIVER, 0xA203, "OWNER_SYS_I2C_REMOVE_DRIVER") \
  X(OWNER_SYS_I2C_DEVICE_PRESENT, 0xA204, "OWNER_SYS_I2C_DEVICE_PRESENT")

#define SYS_ERROR_I2C_MAP(X) X(ERR_I2C_DEV_NOT_FOUND, struct { uint8_t device_address; })
