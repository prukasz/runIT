#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  // Config state
  bool mode_val;
  bool ocpm_val;
  bool nsleep_val;

  // Outputs state
  uint16_t in_duty[4];
  bool en_state[4];
} drv8962_driver_t;

void drv8962_driver_init(drv8962_driver_t* drv, bool mode_val, bool ocpm_val);
