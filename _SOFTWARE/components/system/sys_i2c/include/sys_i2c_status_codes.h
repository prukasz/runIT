#pragma once

#define SYS_I2C_OWNER_MAP(X)                                                                \
  X(OWNER_SYS_I2C_BASE, 0xA200, "OWNER_SYS_I2C_BASE")                                       \
  X(OWNER_SYS_I2C_MASTER_TRANSMIT, 0xA201, "OWNER_SYS_I2C_MASTER_TRANSMIT")                 \
  X(OWNER_SYS_I2C_MASTER_TRANSMIT_RECEIVE, 0xA202, "OWNER_SYS_I2C_MASTER_TRANSMIT_RECEIVE") \
  X(OWNER_SYS_I2C_ADD_DRIVER, 0xA203, "OWNER_SYS_I2C_ADD_DRIVER")                           \
  X(OWNER_SYS_I2C_REMOVE_DRIVER, 0xA204, "OWNER_SYS_I2C_REMOVE_DRIVER")                     \
  X(OWNER_SYS_I2C_INIT, 0XA206, "OWNER_SYS_I2C_INIT")                                       \
  X(OWNER_SYS_I2C_DEVICE_PRESENT, 0XA207, "OWNER_SYS_I2C_DEVICE_PRESENT")

#define SYS_I2C_ERROR_MAP(X)                                                                                                                    \
  X(ERR_SYS_I2C_BASE, 0xA200, "ERR_SYS_I2C_BASE")                                                                                         \
  X(ERR_I2C_TRANSMISSION_FAILURE, 0xA201, "ERR_I2C_TRANSMISSION_FAILURE")                                                                 \
  X(ERR_I2C_DEV_NOT_FOUND, 0xA202, "ERR_I2C_DEV_NOT_FOUND")                                                                 \
  X(ERR_I2C_TIMEOUT, 0xA203, "ERR_I2C_TIMEOUT")
