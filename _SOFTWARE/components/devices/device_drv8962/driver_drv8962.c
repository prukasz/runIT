#include "driver_drv8962.h"

void drv8962_driver_init(drv8962_driver_t* drv, bool mode_val, bool ocpm_val) {
  drv->mode_val = mode_val;
  drv->ocpm_val = ocpm_val;
  drv->nsleep_val = true;
  for (int i = 0; i < 4; i++) {
    drv->in_duty[i] = 0;
    drv->en_state[i] = false;
  }
}
