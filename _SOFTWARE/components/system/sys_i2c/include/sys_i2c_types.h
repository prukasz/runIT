#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

/** One chip on an I2C bus. Embedded by value in the owner's state; nothing needs to be first. */
typedef struct sys_i2c_dev_t {
  i2c_master_dev_handle_t handle;  // NULL until sys_i2c_dev_add()
  uint32_t speed_hz;
  uint8_t addr;
  bool bus1;
} sys_i2c_dev_t;
